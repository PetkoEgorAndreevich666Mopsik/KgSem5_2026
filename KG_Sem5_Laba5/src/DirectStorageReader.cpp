#include "DirectStorageReader.h"

#include "d3dUtil.h"
#include <dstorage.h>
#include <algorithm>

using Microsoft::WRL::ComPtr;

struct DirectStorageReader::Queue {
    ComPtr<IDStorageFactory> factory;
    ComPtr<IDStorageQueue1> queue;
    ComPtr<IDStorageStatusArray> status;
    HANDLE event = nullptr;

    ~Queue() {
        if (event) CloseHandle(event);
    }
};

DirectStorageReader::DirectStorageReader() : mQueue(std::make_unique<Queue>()) {
    ThrowIfFailed(DStorageGetFactory(IID_PPV_ARGS(&mQueue->factory)), "Create DirectStorage factory");
    DSTORAGE_QUEUE_DESC desc{};
    desc.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
    desc.Capacity = DSTORAGE_MIN_QUEUE_CAPACITY;
    desc.Priority = DSTORAGE_PRIORITY_NORMAL;
    desc.Name = "Lab 5 model and textures";
    ThrowIfFailed(mQueue->factory->CreateQueue(&desc, IID_PPV_ARGS(&mQueue->queue)),
                  "Create DirectStorage file queue");
    ThrowIfFailed(mQueue->factory->CreateStatusArray(1, desc.Name, IID_PPV_ARGS(&mQueue->status)),
                  "Create DirectStorage status array");
    mQueue->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!mQueue->event) ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()), "Create DirectStorage event");
}

DirectStorageReader::~DirectStorageReader() = default;

std::vector<uint8_t> DirectStorageReader::Read(const std::filesystem::path& path) {
    ComPtr<IDStorageFile> file;
    const auto absolutePath = std::filesystem::absolute(path).lexically_normal();
    ThrowIfFailed(mQueue->factory->OpenFile(absolutePath.c_str(), IID_PPV_ARGS(&file)), "Open DirectStorage file");
    BY_HANDLE_FILE_INFORMATION info{};
    ThrowIfFailed(file->GetFileInformation(&info), "Get DirectStorage file size");
    if (info.nFileSizeHigh || !info.nFileSizeLow)
        throw std::runtime_error("Unsupported DirectStorage file size");

    // shortcut: read to RAM for Assimp/DDS parsing, use prepacked GPU data for direct-to-VRAM streaming.
    std::vector<uint8_t> data(info.nFileSizeLow);
    constexpr uint32_t chunkSize = 4 * 1024 * 1024;
    for (uint32_t offset = 0; offset < data.size();) {
        const auto bytes = std::min<uint32_t>(chunkSize, info.nFileSizeLow - offset);
        DSTORAGE_REQUEST request{};
        request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
        request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_MEMORY;
        request.Source.File = {file.Get(), offset, bytes};
        request.Destination.Memory = {data.data() + offset, bytes};
        request.UncompressedSize = bytes;
        mQueue->queue->EnqueueRequest(&request);
        offset += bytes;
    }
    mQueue->queue->EnqueueStatus(mQueue->status.Get(), 0);
    mQueue->queue->EnqueueSetEvent(mQueue->event);
    mQueue->queue->Submit();
    if (WaitForSingleObject(mQueue->event, INFINITE) != WAIT_OBJECT_0)
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()), "Wait for DirectStorage completion");
    ThrowIfFailed(mQueue->status->GetHResult(0), "Read DirectStorage file");
    return data;
}
