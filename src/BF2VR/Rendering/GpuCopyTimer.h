#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <Timing.h>
namespace BF2VR {
class GpuCopyTimer {
    struct Slot {
        Microsoft::WRL::ComPtr<ID3D11Query> disjoint, begin, end;
        bool pending = false;
    };
    std::array<Slot, 8> slots;
    bool available = false;
public:
    void Initialize(ID3D11Device* device) {
        for (auto& slot : slots) {
            D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
            if (FAILED(device->CreateQuery(&desc, &slot.disjoint))) return;
            desc.Query = D3D11_QUERY_TIMESTAMP;
            if (FAILED(device->CreateQuery(&desc, &slot.begin)) || FAILED(device->CreateQuery(&desc, &slot.end))) return;
        }
        available = true;
    }
    int Begin(ID3D11DeviceContext* context) {
        if (!available) return -1;
        for (auto& slot : slots) if (slot.pending) {
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{}; UINT64 begin=0,end=0;
            // Never flush or wait for instrumentation. Skip busy slots.
            if (context->GetData(slot.disjoint.Get(), &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
                context->GetData(slot.begin.Get(), &begin, sizeof(begin), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
                context->GetData(slot.end.Get(), &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK) {
                if (!disjoint.Disjoint && disjoint.Frequency && end >= begin)
                    Timing::Add(Timing::CopyGPU, static_cast<uint64_t>((end-begin)*1e9/disjoint.Frequency));
                slot.pending = false;
            }
        }
        for (int i=0;i<8;++i) if (!slots[i].pending) {
            context->Begin(slots[i].disjoint.Get()); context->End(slots[i].begin.Get()); return i;
        }
        return -1;
    }
    void End(ID3D11DeviceContext* context, int index) {
        if (index < 0) return;
        auto& slot = slots[index];
        context->End(slot.end.Get()); context->End(slot.disjoint.Get()); slot.pending = true;
    }
};
}
