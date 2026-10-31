#include "AppAudioCapture.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <audioclient.h>
 #include <audioclientactivationparams.h>
 #include <audiopolicy.h>
 #include <mmdeviceapi.h>
 #include <wrl/client.h>
 #include <wrl/implements.h>

 #pragma comment(lib, "Mmdevapi.lib")
 #pragma comment(lib, "ole32.lib")
#endif

using namespace juce;

#if JUCE_WINDOWS
namespace
{
using Microsoft::WRL::ComPtr;

// Waits for ActivateAudioInterfaceAsync to finish (it calls back on a worker thread).
class ActivationHandler : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                                              Microsoft::WRL::FtmBase, IActivateAudioInterfaceCompletionHandler>
{
public:
    ActivationHandler() { done = CreateEventW(nullptr, TRUE, FALSE, nullptr); }
    ~ActivationHandler() override { CloseHandle(done); }

    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation* operation) override
    {
        HRESULT activateResult = E_FAIL;
        ComPtr<IUnknown> unknown;
        result = operation->GetActivateResult(&activateResult, &unknown);
        if (SUCCEEDED(result))
            result = activateResult;
        if (SUCCEEDED(result))
            result = unknown.As(&client);
        SetEvent(done);
        return S_OK;
    }

    HANDLE done = nullptr;
    HRESULT result = E_FAIL;
    ComPtr<IAudioClient> client;
};

String processName(DWORD pid)
{
    String name;
    if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))
    {
        wchar_t path[MAX_PATH] {};
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(process, 0, path, &size))
            name = File(String(path)).getFileNameWithoutExtension();
        CloseHandle(process);
    }
    return name;
}
} // namespace

// The recording thread: activates a process-loopback client, then copies its packets until it is told to stop or the
// maximum length is reached.
class AppAudioCapture::Worker : public Thread
{
public:
    Worker(AppAudioCapture& o, const Source& s) : Thread("BitVibe app recorder"), owner(o), source(s) {}

    void run() override
    {
        const bool comStarted = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        String error = record();
        if (comStarted)
            CoUninitialize();
        const ScopedLock sl(owner.errorLock);
        owner.lastError = error;
        owner.recording.store(false, std::memory_order_release);
    }

    String startError; // set before the thread is released if activation failed
    WaitableEvent started;

private:
    String record()
    {
        AUDIOCLIENT_ACTIVATION_PARAMS params {};
        params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        params.ProcessLoopbackParams.TargetProcessId = source.pid;
        params.ProcessLoopbackParams.ProcessLoopbackMode = source.exclude ? PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE
                                                                          : PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
        PROPVARIANT activateParams {};
        activateParams.vt = VT_BLOB;
        activateParams.blob.cbSize = sizeof(params);
        activateParams.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

        auto handler = Microsoft::WRL::Make<ActivationHandler>();
        ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
        HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &activateParams,
                                                 handler.Get(), &operation);
        if (FAILED(hr))
            return fail("Windows could not start recording that program (" + String::toHexString((int) hr) + ").");
        WaitForSingleObject(handler->done, 10000);
        if (FAILED(handler->result) || handler->client == nullptr)
            return fail("Windows refused to record that program (" + String::toHexString((int) handler->result) + "). "
                        "Recording a program needs Windows 10 version 2004 or later.");
        auto client = handler->client;

        WAVEFORMATEX format {};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 2;
        format.nSamplesPerSec = (DWORD) kSampleRate;
        format.wBitsPerSample = 16;
        format.nBlockAlign = (WORD) (format.nChannels * format.wBitsPerSample / 8);
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                                2000000 /* 200 ms */, 0, &format, nullptr);
        if (FAILED(hr))
            return fail("The recorder could not be set up (" + String::toHexString((int) hr) + ").");
        HANDLE packetReady = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        client->SetEventHandle(packetReady);
        ComPtr<IAudioCaptureClient> capture;
        hr = client->GetService(IID_PPV_ARGS(&capture));
        if (SUCCEEDED(hr))
            hr = client->Start();
        if (FAILED(hr))
        {
            CloseHandle(packetReady);
            return fail("The recorder could not start (" + String::toHexString((int) hr) + ").");
        }
        started.signal();

        const auto maxFrames = (int64) (kMaxSeconds * kSampleRate);
        bool havePosition = false;
        UINT64 firstPosition = 0;
        String error;
        while (!threadShouldExit())
        {
            if (WaitForSingleObject(packetReady, 200) != WAIT_OBJECT_0)
                continue;
            UINT32 packet = 0;
            while (SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet > 0)
            {
                BYTE* data = nullptr;
                UINT32 count = 0;
                DWORD flags = 0;
                UINT64 position = 0;
                if (FAILED(capture->GetBuffer(&data, &count, &flags, &position, nullptr)))
                    break;
                // While the program is quiet Windows may send nothing at all: the stream position then jumps, and the
                // gap is filled with silence so the recording keeps its timing.
                if (!havePosition)
                {
                    firstPosition = position;
                    havePosition = true;
                }
                const auto expected = (int64) (position - firstPosition);
                const auto have = (int64) owner.recordedL.size();
                if (expected > have && expected < maxFrames)
                {
                    owner.recordedL.resize((size_t) expected, 0.0f);
                    owner.recordedR.resize((size_t) expected, 0.0f);
                }
                const auto* samples = reinterpret_cast<const int16_t*>(data);
                float loudest = 0.0f;
                for (UINT32 i = 0; i < count; ++i)
                {
                    const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                    const float l = silent ? 0.0f : (float) samples[2 * i] / 32768.0f;
                    const float r = silent ? 0.0f : (float) samples[2 * i + 1] / 32768.0f;
                    owner.recordedL.push_back(l);
                    owner.recordedR.push_back(r);
                    loudest = jmax(loudest, std::abs(l), std::abs(r));
                }
                capture->ReleaseBuffer(count);
                owner.frames.store((int64) owner.recordedL.size(), std::memory_order_relaxed);
                if (loudest > owner.peak.load(std::memory_order_relaxed))
                    owner.peak.store(loudest, std::memory_order_relaxed);
            }
            if ((int64) owner.recordedL.size() >= maxFrames)
                break; // the longest recording the sampler takes
        }
        client->Stop();
        CloseHandle(packetReady);
        return error;
    }

    String fail(const String& message)
    {
        startError = message;
        started.signal();
        return message;
    }

    AppAudioCapture& owner;
    Source source;
};

bool AppAudioCapture::isAvailable()
{
    // Process loopback arrived with Windows 10 build 19041 (version 2004).
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    if (auto* ntdll = GetModuleHandleW(L"ntdll.dll"))
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")))
        {
            RTL_OSVERSIONINFOW info {};
            info.dwOSVersionInfoSize = sizeof(info);
            if (fn(&info) == 0)
                return info.dwMajorVersion > 10 || (info.dwMajorVersion == 10 && info.dwBuildNumber >= 19041);
        }
    return false;
}

Array<AppAudioCapture::Source> AppAudioCapture::listSources()
{
    Array<Source> list;
    const DWORD self = GetCurrentProcessId();
    list.add({ "Everything except " + processName(self), (uint32) self, true });
    if (!isAvailable())
        return list;

    const bool comStarted = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    {
        ComPtr<IMMDeviceEnumerator> devices;
        ComPtr<IMMDevice> output;
        ComPtr<IAudioSessionManager2> manager;
        ComPtr<IAudioSessionEnumerator> sessions;
        int count = 0;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices)))
            && SUCCEEDED(devices->GetDefaultAudioEndpoint(eRender, eConsole, &output))
            && SUCCEEDED(output->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(manager.GetAddressOf())))
            && SUCCEEDED(manager->GetSessionEnumerator(&sessions)) && SUCCEEDED(sessions->GetCount(&count)))
        {
            Array<uint32> seen;
            for (int i = 0; i < count; ++i)
            {
                ComPtr<IAudioSessionControl> control;
                ComPtr<IAudioSessionControl2> control2;
                DWORD pid = 0;
                if (FAILED(sessions->GetSession(i, &control)) || FAILED(control.As(&control2)) || FAILED(control2->GetProcessId(&pid)))
                    continue;
                if (pid == 0 || control2->IsSystemSoundsSession() == S_OK || seen.contains((uint32) pid))
                    continue;
                seen.add((uint32) pid);
                auto name = processName(pid);
                if (name.isEmpty())
                    name = "Program " + String((int) pid);
                if (pid == self)
                    name << " (this host)";
                list.add({ name, (uint32) pid, false });
            }
        }
    }
    if (comStarted)
        CoUninitialize();
    std::sort(list.begin() + 1, list.end(), [](const Source& a, const Source& b) { return a.name.compareNatural(b.name) < 0; });
    return list;
}

bool AppAudioCapture::start(const Source& source, String& error)
{
    stop();
    if (!isAvailable())
    {
        error = "Recording a program needs Windows 10 version 2004 or later.";
        return false;
    }
    recordedL.clear();
    recordedR.clear();
    recordedL.reserve((size_t) (kMaxSeconds * kSampleRate));
    recordedR.reserve((size_t) (kMaxSeconds * kSampleRate));
    frames.store(0);
    peak.store(0.0f);
    {
        const ScopedLock sl(errorLock);
        lastError = {};
    }
    recording.store(true, std::memory_order_release);
    worker = std::make_unique<Worker>(*this, source);
    worker->startThread();
    worker->started.wait(12000);
    if (worker->startError.isNotEmpty())
    {
        error = worker->startError;
        stop();
        return false;
    }
    return true;
}

void AppAudioCapture::stop()
{
    if (worker != nullptr)
    {
        worker->stopThread(3000);
        worker.reset();
    }
    recording.store(false, std::memory_order_release);
}

#else // not Windows: the sampler cannot record programs yet

class AppAudioCapture::Worker
{
};

bool AppAudioCapture::isAvailable() { return false; }
Array<AppAudioCapture::Source> AppAudioCapture::listSources() { return {}; }

bool AppAudioCapture::start(const Source&, String& error)
{
    error = "Recording a program is only available on Windows for now.";
    return false;
}

void AppAudioCapture::stop() { recording.store(false); }

#endif

AppAudioCapture::AppAudioCapture() = default;

AppAudioCapture::~AppAudioCapture()
{
    stop();
}

String AppAudioCapture::getLastError() const
{
    const ScopedLock sl(errorLock);
    return lastError;
}

void AppAudioCapture::takeRecording(std::vector<float>& left, std::vector<float>& right)
{
    jassert(!isRecording());
    left = recordedL;
    right = recordedR;
}
