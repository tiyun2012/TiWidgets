#pragma once

#include "core_types.h"
#include <Windows.h>
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <array>
#include <string>

#ifdef DFRect
#undef DFRect
#endif

class DX12Canvas : public Canvas {
public:
    static constexpr int FONT_ATLAS_WIDTH = 1024;
    static constexpr int FONT_ATLAS_HEIGHT = 4096;
    static constexpr int FONT_MIN_PX = 8;
    static constexpr int FONT_MAX_PX = 32;
    static constexpr int FONT_CELL_WIDTH = 32;
    static constexpr int FONT_CELL_HEIGHT = 48;
    struct D3DVertex {
        float position[2];
        float color[4];
        float uv[2] = {0.5f / FONT_ATLAS_WIDTH, 0.5f / FONT_ATLAS_HEIGHT};
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
    void drawTextScaled(float x, float y, const std::string& text, const DFColor& color, float scale, bool smooth) override;

    void setRenderSize(float w, float h) { targetWidth_ = w; targetHeight_ = h; }
    void flush();
    void clear();

private:
    void initializePipeline();
    void createVertexBuffer(size_t vertexCount);
    void initializeFontAtlas();

    ID3D12Device* device_;
    ID3D12GraphicsCommandList* commandList_;
    float targetWidth_;
    float targetHeight_;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> fontAtlas_;
    Microsoft::WRL::ComPtr<ID3D12Resource> fontUpload_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> fontHeap_;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fontFootprint_{};
    bool fontUploaded_ = false;
    std::array<int, FONT_MAX_PX - FONT_MIN_PX + 1> fontTopOffsets_{};
    bool flushedThisFrame_ = false;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> submittedBuffers_;

    std::vector<D3DVertex> vertices_;
    static constexpr size_t MAX_VERTICES = 65536;
};

