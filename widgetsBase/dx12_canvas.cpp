#include "dx12_canvas.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace {
constexpr float kPi = 3.14159265358979323846f;
const char* kVS = R"(
cbuffer View : register(b0)
{
    float2 uScreenSize;
};

struct VS_INPUT {
    float2 pos : POSITION;
    float4 col : COLOR;
    float2 uv : TEXCOORD;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float4 col : COLOR;
    float2 uv : TEXCOORD;
};

PS_INPUT main(VS_INPUT input) {
    PS_INPUT output;
    float2 ndc = float2(
        (input.pos.x / uScreenSize.x) * 2.0f - 1.0f,
        1.0f - (input.pos.y / uScreenSize.y) * 2.0f);
    output.pos = float4(ndc, 0.0f, 1.0f);
    output.col = input.col;
    output.uv = input.uv;
    return output;
}
)";

const char* kPS = R"(
Texture2D atlas : register(t0);
SamplerState atlasSampler : register(s0);
struct PS_INPUT {
    float4 pos : SV_POSITION;
    float4 col : COLOR;
    float2 uv : TEXCOORD;
};

float4 main(PS_INPUT input) : SV_Target {
    return float4(input.col.rgb, input.col.a * atlas.Sample(atlasSampler, input.uv).r);
}
)";
}

DX12Canvas::DX12Canvas(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, float targetWidth, float targetHeight)
    : device_(device), commandList_(commandList), targetWidth_(targetWidth), targetHeight_(targetHeight)
{
    initializePipeline();
    createVertexBuffer(MAX_VERTICES);
    initializeFontAtlas();
}

void DX12Canvas::initializePipeline()
{
    ComPtr<ID3DBlob> vs, ps, errors;
    HRESULT hr = D3DCompile(kVS, strlen(kVS), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vs, &errors);
    if (FAILED(hr)) {
        if (errors) OutputDebugStringA((char*)errors->GetBufferPointer());
        throw std::runtime_error("Failed to compile vertex shader");
    }
    hr = D3DCompile(kPS, strlen(kPS), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &ps, &errors);
    if (FAILED(hr)) {
        if (errors) OutputDebugStringA((char*)errors->GetBufferPointer());
        throw std::runtime_error("Failed to compile pixel shader");
    }

    // Root signature: one CBV (b0) and IA only.
    D3D12_ROOT_PARAMETER param{};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    param.Constants.ShaderRegister = 0;    // b0
    param.Constants.RegisterSpace = 0;
    param.Constants.Num32BitValues = 2;    // float2 screen size
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER params[2] = {param, {}};
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &range;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = 2;
    rsDesc.pParameters = params;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers = &sampler;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sig;
    hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &errors);
    if (FAILED(hr)) throw std::runtime_error("Failed to serialize root signature");
    hr = device_->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
    if (FAILED(hr)) throw std::runtime_error("Failed to create root signature");

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_RASTERIZER_DESC rast{};
    rast.FillMode = D3D12_FILL_MODE_SOLID;
    rast.CullMode = D3D12_CULL_MODE_NONE;
    rast.FrontCounterClockwise = FALSE;
    rast.DepthClipEnable = TRUE;

    D3D12_BLEND_DESC blend{};
    blend.AlphaToCoverageEnable = FALSE;
    blend.IndependentBlendEnable = FALSE;
    auto& rt = blend.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = FALSE;
    depth.StencilEnable = FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.InputLayout = { layout, _countof(layout) };
    pso.pRootSignature = rootSignature_.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.RasterizerState = rast;
    pso.BlendState = blend;
    pso.DepthStencilState = depth;
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.SampleDesc.Count = 1;

    hr = device_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pipelineState_));
    if (FAILED(hr)) throw std::runtime_error("Failed to create pipeline state");
}

void DX12Canvas::createVertexBuffer(size_t vertexCount)
{
    const UINT bufferSize = static_cast<UINT>(vertexCount * sizeof(D3DVertex));

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bufferSize;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = device_->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexBuffer_));
    if (FAILED(hr)) throw std::runtime_error("Failed to create vertex buffer");

    vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vertexBufferView_.StrideInBytes = sizeof(D3DVertex);
    vertexBufferView_.SizeInBytes = bufferSize;
}

void DX12Canvas::initializeFontAtlas()
{
    // Rasterize each UI size directly. Sampling pixel-aligned glyphs 1:1 avoids
    // the blur caused by shrinking a single large glyph atlas with linear filtering.
    std::vector<uint8_t> pixels(FONT_ATLAS_WIDTH * FONT_ATLAS_HEIGHT, 0);
    pixels[0] = pixels[1] = pixels[FONT_ATLAS_WIDTH] = pixels[FONT_ATLAS_WIDTH + 1] = 255;
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) throw std::runtime_error("Cannot create font DC");
    MAT2 transform{};
    transform.eM11.value = transform.eM22.value = 1;
    for (int size = FONT_MIN_PX; size <= FONT_MAX_PX; ++size) {
        HFONT font = CreateFontW(-size, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY, FIXED_PITCH, L"Consolas");
        if (!font) { DeleteDC(dc); throw std::runtime_error("Cannot create UI font"); }
        HGDIOBJ oldFont = SelectObject(dc, font);
        TEXTMETRICW textMetrics{};
        GetTextMetricsW(dc, &textMetrics);
        GLYPHMETRICS capital{};
        GetGlyphOutlineW(dc, 'H', GGO_METRICS, &capital, 0, nullptr, &transform);
        const int baseline = textMetrics.tmAscent + 1;
        const int sizeIndex = size - FONT_MIN_PX;
        fontTopOffsets_[sizeIndex] = baseline - static_cast<int>(capital.gmBlackBoxY);
        for (UINT ch = 32; ch < 127; ++ch) {
            GLYPHMETRICS metrics{};
            const DWORD bytes = GetGlyphOutlineW(dc, ch, GGO_GRAY8_BITMAP, &metrics, 0, nullptr, &transform);
            if (bytes == GDI_ERROR || bytes == 0) continue;
            std::vector<uint8_t> glyph(bytes);
            if (GetGlyphOutlineW(dc, ch, GGO_GRAY8_BITMAP, &metrics, bytes, glyph.data(), &transform) == GDI_ERROR) continue;
            const UINT stride = (metrics.gmBlackBoxX + 3) & ~3u;
            const int cellX = static_cast<int>((ch - 32) % 32) * FONT_CELL_WIDTH;
            const int cellY = (1 + sizeIndex * 3 + static_cast<int>((ch - 32) / 32)) * FONT_CELL_HEIGHT;
            for (UINT y = 0; y < metrics.gmBlackBoxY; ++y) {
                for (UINT x = 0; x < metrics.gmBlackBoxX; ++x) {
                    const int px = cellX + metrics.gmptGlyphOrigin.x + static_cast<int>(x);
                    const int py = cellY + baseline - metrics.gmptGlyphOrigin.y + static_cast<int>(y);
                    if (px >= cellX && px < cellX + FONT_CELL_WIDTH && py >= cellY && py < cellY + FONT_CELL_HEIGHT)
                        pixels[py * FONT_ATLAS_WIDTH + px] = static_cast<uint8_t>(std::min(255u, static_cast<UINT>(glyph[y * stride + x]) * 255u / 64u));
                }
            }
        }
        SelectObject(dc, oldFont);
        DeleteObject(font);
    }
    DeleteDC(dc);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC texture{};
    texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture.Width = FONT_ATLAS_WIDTH;
    texture.Height = FONT_ATLAS_HEIGHT;
    texture.DepthOrArraySize = texture.MipLevels = 1;
    texture.Format = DXGI_FORMAT_R8_UNORM;
    texture.SampleDesc.Count = 1;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&fontAtlas_))))
        throw std::runtime_error("Cannot create font texture");
    UINT64 totalBytes = 0;
    device_->GetCopyableFootprints(&texture, 0, 1, 0, &fontFootprint_, nullptr, nullptr, &totalBytes);
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC upload{};
    upload.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    upload.Width = totalBytes;
    upload.Height = upload.DepthOrArraySize = upload.MipLevels = 1;
    upload.SampleDesc.Count = 1;
    upload.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &upload,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&fontUpload_))))
        throw std::runtime_error("Cannot upload font texture");
    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange{0, 0};
    if (FAILED(fontUpload_->Map(0, &readRange, reinterpret_cast<void**>(&mapped))))
        throw std::runtime_error("Cannot map font upload");
    for (size_t y = 0; y < FONT_ATLAS_HEIGHT; ++y)
        std::memcpy(mapped + fontFootprint_.Offset + y * fontFootprint_.Footprint.RowPitch, pixels.data() + y * FONT_ATLAS_WIDTH, FONT_ATLAS_WIDTH);
    fontUpload_->Unmap(0, nullptr);
    D3D12_DESCRIPTOR_HEAP_DESC descriptor{};
    descriptor.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    descriptor.NumDescriptors = 1;
    descriptor.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device_->CreateDescriptorHeap(&descriptor, IID_PPV_ARGS(&fontHeap_))))
        throw std::runtime_error("Cannot create font descriptor");
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = texture.Format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(fontAtlas_.Get(), &srv, fontHeap_->GetCPUDescriptorHandleForHeapStart());
}

void DX12Canvas::drawTextScaled(float x, float y, const std::string& text, const DFColor& color, float scale, bool /*smooth*/)
{
    const float requestedSize = 10.0f * DFTextPixelScale() * std::clamp(scale, 0.2f, 4.0f);
    const int size = std::clamp(static_cast<int>(std::lround(requestedSize)), FONT_MIN_PX, FONT_MAX_PX);
    const int sizeIndex = size - FONT_MIN_PX;
    // Sizes outside the UI atlas range retain the public Canvas scaling behavior.
    const float magnification = (requestedSize < FONT_MIN_PX || requestedSize > FONT_MAX_PX)
        ? requestedSize / size : 1.0f;
    const float startX = x;
    for (unsigned char ch : text) {
        if (ch == '\n') { x = startX; y += std::round(requestedSize); continue; }
        if (ch < 32 || ch >= 127) ch = '?';
        if (vertices_.size() + 6 > MAX_VERTICES) flush();
        const float u = static_cast<float>((ch - 32) % 32 * FONT_CELL_WIDTH) / FONT_ATLAS_WIDTH;
        const float v = static_cast<float>((1 + sizeIndex * 3 + (ch - 32) / 32) * FONT_CELL_HEIGHT) / FONT_ATLAS_HEIGHT;
        auto vertex = [&](float px, float py, float tx, float ty) {
            return D3DVertex{{px, py}, {color.r, color.g, color.b, color.a}, {tx, ty}};
        };
        const float left = std::round(x);
        const float top = std::round(y) - fontTopOffsets_[sizeIndex] * magnification;
        const float width = FONT_CELL_WIDTH * magnification;
        const float height = FONT_CELL_HEIGHT * magnification;
        const auto a = vertex(left, top, u, v);
        const auto b = vertex(left + width, top, u + static_cast<float>(FONT_CELL_WIDTH) / FONT_ATLAS_WIDTH, v);
        const auto c = vertex(left, top + height, u, v + static_cast<float>(FONT_CELL_HEIGHT) / FONT_ATLAS_HEIGHT);
        const auto d = vertex(left + width, top + height, u + static_cast<float>(FONT_CELL_WIDTH) / FONT_ATLAS_WIDTH, v + static_cast<float>(FONT_CELL_HEIGHT) / FONT_ATLAS_HEIGHT);
        vertices_.insert(vertices_.end(), {a, b, c, b, d, c});
        x += DFGlyphAdvancePx(scale);
    }
}

void DX12Canvas::drawRectangle(const DFRect& rect, const DFColor& color)
{
    if (rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }
    if (vertices_.size() + 6 > MAX_VERTICES) flush();
    auto makeV = [&](float x, float y) {
        return D3DVertex{{x, y}, {color.r, color.g, color.b, color.a}};
    };
    D3DVertex v1 = makeV(rect.x, rect.y);
    D3DVertex v2 = makeV(rect.x + rect.width, rect.y);
    D3DVertex v3 = makeV(rect.x, rect.y + rect.height);
    D3DVertex v4 = makeV(rect.x + rect.width, rect.y + rect.height);
    vertices_.push_back(v1); vertices_.push_back(v2); vertices_.push_back(v3);
    vertices_.push_back(v2); vertices_.push_back(v4); vertices_.push_back(v3);
}

void DX12Canvas::drawRoundedRectangle(const DFRect& rect, float radius, const DFColor& color)
{
    if (rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }

    const float maxRadius = std::min(rect.width, rect.height) * 0.5f;
    const float r = std::clamp(radius, 0.0f, maxRadius);
    if (r <= 0.01f) {
        drawRectangle(rect, color);
        return;
    }

    auto makeVertex = [&](float x, float y) {
        return D3DVertex{{x, y}, {color.r, color.g, color.b, color.a}};
    };
    auto addTriangle = [&](const D3DVertex& a, const D3DVertex& b, const D3DVertex& c) {
        if (vertices_.size() + 3 > MAX_VERTICES) {
            flush();
        }
        vertices_.push_back(a);
        vertices_.push_back(b);
        vertices_.push_back(c);
    };
    auto addRect = [&](float x, float y, float w, float h) {
        if (w <= 0.0f || h <= 0.0f) {
            return;
        }
        D3DVertex v1 = makeVertex(x, y);
        D3DVertex v2 = makeVertex(x + w, y);
        D3DVertex v3 = makeVertex(x, y + h);
        D3DVertex v4 = makeVertex(x + w, y + h);
        addTriangle(v1, v2, v3);
        addTriangle(v2, v4, v3);
    };

    addRect(rect.x + r, rect.y + r, rect.width - r * 2.0f, rect.height - r * 2.0f);
    addRect(rect.x + r, rect.y, rect.width - r * 2.0f, r);
    addRect(rect.x + r, rect.y + rect.height - r, rect.width - r * 2.0f, r);
    addRect(rect.x, rect.y + r, r, rect.height - r * 2.0f);
    addRect(rect.x + rect.width - r, rect.y + r, r, rect.height - r * 2.0f);

    const int segments = std::max(6, static_cast<int>(std::ceil(r * 0.75f)));
    auto addCornerFan = [&](float cx, float cy, float startAngle, float endAngle) {
        const D3DVertex center = makeVertex(cx, cy);
        for (int i = 0; i < segments; ++i) {
            const float t0 = static_cast<float>(i) / static_cast<float>(segments);
            const float t1 = static_cast<float>(i + 1) / static_cast<float>(segments);
            const float a0 = startAngle + (endAngle - startAngle) * t0;
            const float a1 = startAngle + (endAngle - startAngle) * t1;
            const D3DVertex p0 = makeVertex(cx + std::cos(a0) * r, cy + std::sin(a0) * r);
            const D3DVertex p1 = makeVertex(cx + std::cos(a1) * r, cy + std::sin(a1) * r);
            addTriangle(center, p0, p1);
        }
    };

    addCornerFan(rect.x + r, rect.y + r, kPi, kPi * 1.5f);
    addCornerFan(rect.x + rect.width - r, rect.y + r, kPi * 1.5f, kPi * 2.0f);
    addCornerFan(rect.x + rect.width - r, rect.y + rect.height - r, 0.0f, kPi * 0.5f);
    addCornerFan(rect.x + r, rect.y + rect.height - r, kPi * 0.5f, kPi);
}

void DX12Canvas::drawRoundedRectangleOutline(const DFRect& rect, float radius, const DFColor& color, float thickness)
{
    if (rect.width <= 0.0f || rect.height <= 0.0f || thickness <= 0.0f) {
        return;
    }

    const float maxRadius = std::min(rect.width, rect.height) * 0.5f;
    const float r = std::clamp(radius, 0.0f, maxRadius);
    const float t = std::max(0.5f, thickness);

    if (r <= 0.01f) {
        drawRectangle({rect.x, rect.y, rect.width, t}, color);
        drawRectangle({rect.x, rect.y + std::max(0.0f, rect.height - t), rect.width, t}, color);
        drawRectangle({rect.x, rect.y, t, rect.height}, color);
        drawRectangle({rect.x + std::max(0.0f, rect.width - t), rect.y, t, rect.height}, color);
        return;
    }

    drawLine({rect.x + r, rect.y}, {rect.x + rect.width - r, rect.y}, color, t);
    drawLine({rect.x + r, rect.y + rect.height}, {rect.x + rect.width - r, rect.y + rect.height}, color, t);
    drawLine({rect.x, rect.y + r}, {rect.x, rect.y + rect.height - r}, color, t);
    drawLine({rect.x + rect.width, rect.y + r}, {rect.x + rect.width, rect.y + rect.height - r}, color, t);

    const int segments = std::max(8, static_cast<int>(std::ceil(r)));
    auto drawArc = [&](float cx, float cy, float startAngle, float endAngle) {
        DFPoint previous{cx + std::cos(startAngle) * r, cy + std::sin(startAngle) * r};
        for (int i = 1; i <= segments; ++i) {
            const float tNorm = static_cast<float>(i) / static_cast<float>(segments);
            const float angle = startAngle + (endAngle - startAngle) * tNorm;
            DFPoint current{cx + std::cos(angle) * r, cy + std::sin(angle) * r};
            drawLine(previous, current, color, t);
            previous = current;
        }
    };

    drawArc(rect.x + r, rect.y + r, kPi, kPi * 1.5f);
    drawArc(rect.x + rect.width - r, rect.y + r, kPi * 1.5f, kPi * 2.0f);
    drawArc(rect.x + rect.width - r, rect.y + rect.height - r, 0.0f, kPi * 0.5f);
    drawArc(rect.x + r, rect.y + rect.height - r, kPi * 0.5f, kPi);
}

void DX12Canvas::drawLine(const DFPoint& a, const DFPoint& b, const DFColor& color, float thickness)
{
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.0001f) {
        const float t = std::max(1.0f, thickness);
        drawRectangle({a.x - t * 0.5f, a.y - t * 0.5f, t, t}, color);
        return;
    }

    const float nx = -dy / len;
    const float ny = dx / len;
    const float half = std::max(0.5f, thickness * 0.5f);

    const D3DVertex v1{{a.x + nx * half, a.y + ny * half}, {color.r, color.g, color.b, color.a}};
    const D3DVertex v2{{a.x - nx * half, a.y - ny * half}, {color.r, color.g, color.b, color.a}};
    const D3DVertex v3{{b.x + nx * half, b.y + ny * half}, {color.r, color.g, color.b, color.a}};
    const D3DVertex v4{{b.x - nx * half, b.y - ny * half}, {color.r, color.g, color.b, color.a}};

    if (vertices_.size() + 6 > MAX_VERTICES) flush();
    vertices_.push_back(v1); vertices_.push_back(v2); vertices_.push_back(v3);
    vertices_.push_back(v2); vertices_.push_back(v4); vertices_.push_back(v3);
}

void DX12Canvas::flush()
{
    if (vertices_.empty()) return;

    // Each draw in a command list must keep its own upload storage until the
    // GPU completes. Rewriting the same buffer corrupted frames with many glyphs.
    if (flushedThisFrame_) {
        submittedBuffers_.push_back(vertexBuffer_);
        createVertexBuffer(MAX_VERTICES);
    }
    flushedThisFrame_ = true;
    if (!fontUploaded_) {
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = fontAtlas_.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = fontUpload_.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = fontFootprint_;
        commandList_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = fontAtlas_.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        commandList_->ResourceBarrier(1, &barrier);
        fontUploaded_ = true;
    }

    D3D12_RANGE readRange{0, 0};
    uint8_t* data = nullptr;
    vertexBuffer_->Map(0, &readRange, reinterpret_cast<void**>(&data));
    std::memcpy(data, vertices_.data(), vertices_.size() * sizeof(D3DVertex));
    vertexBuffer_->Unmap(0, nullptr);

    struct ViewCB { float screenSize[2]; float pad[2]; } cb{{targetWidth_, targetHeight_}, {0,0}};
    commandList_->SetGraphicsRootSignature(rootSignature_.Get());
    ID3D12DescriptorHeap* heaps[] = {fontHeap_.Get()};
    commandList_->SetDescriptorHeaps(1, heaps);
    commandList_->SetGraphicsRootDescriptorTable(1, fontHeap_->GetGPUDescriptorHandleForHeapStart());
    commandList_->SetPipelineState(pipelineState_.Get());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList_->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList_->SetGraphicsRoot32BitConstants(0, 2, cb.screenSize, 0);
    commandList_->DrawInstanced(static_cast<UINT>(vertices_.size()), 1, 0, 0);

    vertices_.clear();
}

void DX12Canvas::clear()
{
    // The demo waits for its frame fence before starting the next frame.
    submittedBuffers_.clear();
    flushedThisFrame_ = false;
    vertices_.clear();
}

