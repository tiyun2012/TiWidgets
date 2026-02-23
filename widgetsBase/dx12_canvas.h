#pragma once

#include "core_types.h"
#include <Windows.h>
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <string>

#ifdef DFRect
#undef DFRect
#endif

class DX12Canvas : public Canvas {
public:
    struct D3DVertex {
        float position[2];
        float color[4];
    };

    DX12Canvas(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, float targetWidth, float targetHeight);
    ~DX12Canvas() = default;

    void drawRectangle(const DFRect& rect, const DFColor& color) override;
    void drawRoundedRectangle(const DFRect& rect, float radius, const DFColor& color) override;
    void drawRoundedRectangleOutline(const DFRect& rect, float radius, const DFColor& color, float thickness = 1.0f) override;
    void drawLine(const DFPoint& a, const DFPoint& b, const DFColor& color, float thickness = 1.0f) override;
    void drawText(float x, float y, const std::string& text, const DFColor& color) override
    {
        Canvas::drawText(x, y, text, color);
    }

    void setRenderSize(float w, float h) { targetWidth_ = w; targetHeight_ = h; }
    void flush();
    void clear();

private:
    void initializePipeline();
    void createVertexBuffer(size_t vertexCount);

    ID3D12Device* device_;
    ID3D12GraphicsCommandList* commandList_;
    float targetWidth_;
    float targetHeight_;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    std::vector<D3DVertex> vertices_;
    static constexpr size_t MAX_VERTICES = 65536;
};

