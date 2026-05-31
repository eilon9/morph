#pragma once
#include <JuceHeader.h>

#if JUCE_WINDOWS
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

// -------------------------------------------------------------------
// Shared audio data layout (per listener index)
// Uses a seqlock (sequence counter) for lock-free read/write.
//   writer: seq++ (odd → locked), write data, seq++ (even → unlocked)
//   reader: check seq parity + stability; if inconsistent, skip read.
// -------------------------------------------------------------------
struct ListenerAudioData
{
    static constexpr int maxSamples = 8192;

    // Seqlock generation counter: even = valid snapshot, odd = being written.
    // Incremented twice per write (odd→locked, even→unlocked).
    std::atomic<uint32_t> seq { 0 };
    std::atomic<int>      numSamplesWritten;
    std::atomic<int>      isConnected;          // 1 = listener is alive
    std::atomic<int>      processBlockCount;    // incremented after each complete write
    double                sampleRate;
    int64_t               hostTimeInSamples;
    float                 bufferL[maxSamples];
    float                 bufferR[maxSamples];
};

static constexpr int MaxListeners = 16;

// -------------------------------------------------------------------
// Cross-platform named shared memory
// Uses "Local\" prefix (no admin rights needed) on Windows.
// MorphListener creates + maps its own index, keeps handle open.
// Morph scans indices to find active listeners.
// -------------------------------------------------------------------
namespace SharedMemory
{
    using Handle = void*;
    static const Handle InvalidHandle = nullptr;

    // Create or open a named shared memory region.
    static Handle open (const juce::String& name, size_t size)
    {
        (void) size;

#if JUCE_WINDOWS
        HANDLE h = CreateFileMappingW (
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            (DWORD) (size >> 32),
            (DWORD) size,
            name.toWideCharPointer());

        return (Handle) h;
#else
        int fd = shm_open (name.toUTF8(), O_CREAT | O_RDWR, 0666);
        if (fd < 0)
            return InvalidHandle;
        ftruncate (fd, (off_t) size);
        return (Handle) (intptr_t) fd;
#endif
    }

    // Map into address space
    static void* map (Handle h, size_t size)
    {
        if (h == InvalidHandle)
            return nullptr;

#if JUCE_WINDOWS
        return MapViewOfFile ((HANDLE) h, FILE_MAP_ALL_ACCESS, 0, 0, size);
#else
        void* ptr = mmap (nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                          (int) (intptr_t) h, 0);
        return (ptr == MAP_FAILED) ? nullptr : ptr;
#endif
    }

    static void unmap (void* ptr, size_t size)
    {
        (void) size;
        if (ptr == nullptr)
            return;
#if JUCE_WINDOWS
        UnmapViewOfFile (ptr);
#else
        munmap (ptr, size);
#endif
    }

    static void closeHandle (Handle h)
    {
        if (h == InvalidHandle)
            return;
#if JUCE_WINDOWS
        CloseHandle ((HANDLE) h);
#else
        close ((int) (intptr_t) h);
#endif
    }

    // A persistent mapped region: open on init, close on destroy
    struct MappedRegion
    {
        Handle   handle = InvalidHandle;
        void*    ptr    = nullptr;
        size_t   size   = 0;
        juce::String name;
    };

    // Open + map permanently (caller keeps the MappedRegion alive)
    static MappedRegion create (const juce::String& name, size_t size)
    {
        MappedRegion mr;
        mr.name = name;
        mr.size = size;
        mr.handle = open (name, size);
        if (mr.handle != InvalidHandle)
            mr.ptr = map (mr.handle, size);
        return mr;
    }

    static void destroy (MappedRegion& mr)
    {
        if (mr.ptr != nullptr)
            unmap (mr.ptr, mr.size);
        if (mr.handle != InvalidHandle)
            closeHandle (mr.handle);
        mr.handle = InvalidHandle;
        mr.ptr    = nullptr;
        mr.size   = 0;
    }

    // Read-only open of a data region (for scanning from Morph)
    static MappedRegion openReadOnly (const juce::String& name, size_t size)
    {
        MappedRegion mr;
        mr.name = name;
        mr.size = size;

#if JUCE_WINDOWS
        // For read-only, just use the same r/w open (works fine)
        mr.handle = open (name, size);
        if (mr.handle != InvalidHandle)
            mr.ptr = map (mr.handle, size);
#else
        mr.handle = open (name, size);
        if (mr.handle != InvalidHandle)
            mr.ptr = map (mr.handle, size);
#endif
        return mr;
    }

    static ListenerAudioData* getAudio (const MappedRegion& mr)
    {
        if (mr.ptr == nullptr)
            return nullptr;
        return static_cast<ListenerAudioData*> (mr.ptr);
    }

    // Find a free index by trying each one.
    // Returns the index and fills in the MappedRegion (map kept alive).
    // If none free, returns -1 and mr is untouched.
    static juce::String shmName (int index)
    {
#if JUCE_WINDOWS
        return "Local\\Morph_Data_" + juce::String (index);
#else
        return "/Morph_Data_" + juce::String (index);
#endif
    }

    static int claimFreeIndex (MappedRegion& mr)
    {
        for (int i = 0; i < MaxListeners; ++i)
        {
            juce::String dataName = shmName (i);
            auto candidate = create (dataName, sizeof (ListenerAudioData));
            if (candidate.ptr == nullptr)
                continue;

            auto* audio = getAudio (candidate);
            if (audio == nullptr)
            {
                destroy (candidate);
                continue;
            }

            // Try to claim: if isConnected is 0, set it to 1
            int expected = 0;
            if (audio->isConnected.compare_exchange_strong (expected, 1))
            {
                // Claimed!
                mr = candidate;
                audio->seq.store (0, std::memory_order_release);
                audio->numSamplesWritten.store (0, std::memory_order_relaxed);
                audio->processBlockCount.store (0, std::memory_order_relaxed);
                return i;
            }

            destroy (candidate);
        }
        return -1;
    }

    // Open a specific data index (no claiming, just opening)
    static MappedRegion openIndex (int index)
    {
        juce::String dataName = shmName (index);
        return create (dataName, sizeof (ListenerAudioData));
    }

    // Check if a specific index is active (used by Morph)
    static bool isIndexActive (int index)
    {
        if (index < 0 || index >= MaxListeners)
            return false;

        auto mr = openIndex (index);
        if (mr.ptr == nullptr)
            return false;

        auto* audio = getAudio (mr);
        bool active = (audio != nullptr && audio->isConnected.load() != 0);

        destroy (mr);
        return active;
    }
}