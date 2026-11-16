#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include "controls_capture.h"

namespace {
using Microsoft::WRL::ComPtr;
void Require(HRESULT result) {
    if (FAILED(result)) throw std::runtime_error("PNG encoder failed: " + std::to_string(uint32_t(result)));
}
float Edge(ImVec2 a, ImVec2 b, ImVec2 p) {
    return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}
bool Inclusive(ImVec2 a, ImVec2 b) { return b.y < a.y || (b.y == a.y && b.x > a.x); }
bool Inside(float edge, ImVec2 a, ImVec2 b) { return edge > 0 || (edge == 0 && Inclusive(a, b)); }
void SavePng(const std::filesystem::path& path, int width, int height, std::vector<uint8_t>& pixels) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Require(initialized);
    {
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        Require(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
        Require(factory->CreateStream(&stream));
        Require(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
        Require(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
        Require(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
        Require(encoder->CreateNewFrame(&frame, nullptr));
        Require(frame->Initialize(nullptr));
        Require(frame->SetSize(width, height));
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        Require(frame->SetPixelFormat(&format));
        if (format != GUID_WICPixelFormat32bppBGRA) throw std::runtime_error("PNG encoder changed BGRA format");
        for (size_t i = 0; i < pixels.size(); i += 4) std::swap(pixels[i], pixels[i + 2]);
        Require(frame->WritePixels(height, width * 4, UINT(pixels.size()), pixels.data()));
        Require(frame->Commit());
        Require(encoder->Commit());
    }
    CoUninitialize();
}
}

void CaptureControlsPng(const std::filesystem::path& path, ImVec2 origin, ImVec2 size) {
    const ImDrawData& draw = *ImGui::GetDrawData();
    const int width = int(std::ceil(size.x)), height = int(std::ceil(size.y));
    if (width <= 0 || height <= 0 || draw.FramebufferScale.x != 1 || draw.FramebufferScale.y != 1)
        throw std::runtime_error("Capture requires a positive display size and 1x framebuffer scale");
    std::vector<uint8_t> pixels(size_t(width) * height * 4);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i] = pixels[i + 1] = pixels[i + 2] = 22;
        pixels[i + 3] = 255;
    }
    unsigned char* atlas; int atlas_width, atlas_height;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&atlas, &atlas_width, &atlas_height);
    const auto texture = [&](ImVec2 uv, int channel) {
        const float x = std::clamp(uv.x * atlas_width - .5f, 0.f, float(atlas_width - 1));
        const float y = std::clamp(uv.y * atlas_height - .5f, 0.f, float(atlas_height - 1));
        const int x0 = int(x), y0 = int(y), x1 = std::min(x0 + 1, atlas_width - 1), y1 = std::min(y0 + 1, atlas_height - 1);
        const auto at = [&](int px, int py) { return float(atlas[(py * atlas_width + px) * 4 + channel]); };
        return std::lerp(std::lerp(at(x0, y0), at(x1, y0), x - x0), std::lerp(at(x0, y1), at(x1, y1), x - x0), y - y0);
    };
    for (const ImDrawList* list : draw.CmdLists) for (const ImDrawCmd& command : list->CmdBuffer) {
        if (command.UserCallback) {
            if (command.UserCallback != ImDrawCallback_ResetRenderState) command.UserCallback(list, &command);
            continue;
        }
        if (command.GetTexID() != ImGui::GetIO().Fonts->TexID)
            throw std::runtime_error("Capture encountered a texture other than the font atlas");
        for (unsigned i = 0; i < command.ElemCount; i += 3) {
            const ImDrawVert* v[3];
            for (int j = 0; j < 3; ++j) v[j] = &list->VtxBuffer[list->IdxBuffer[command.IdxOffset + i + j] + command.VtxOffset];
            ImVec2 p[3];
            for (int j = 0; j < 3; ++j) p[j] = ImVec2(v[j]->pos.x - origin.x, v[j]->pos.y - origin.y);
            float area = Edge(p[0], p[1], p[2]);
            if (area == 0) continue;
            if (area < 0) { std::swap(p[1], p[2]); std::swap(v[1], v[2]); area = -area; }
            const int left = std::max({0, int(std::floor(std::min({p[0].x, p[1].x, p[2].x}))), int(std::ceil(command.ClipRect.x - origin.x))});
            const int top = std::max({0, int(std::floor(std::min({p[0].y, p[1].y, p[2].y}))), int(std::ceil(command.ClipRect.y - origin.y))});
            const int right = std::min({width, int(std::ceil(std::max({p[0].x, p[1].x, p[2].x}))), int(std::ceil(command.ClipRect.z - origin.x))});
            const int bottom = std::min({height, int(std::ceil(std::max({p[0].y, p[1].y, p[2].y}))), int(std::ceil(command.ClipRect.w - origin.y))});
            for (int y = top; y < bottom; ++y) for (int x = left; x < right; ++x) {
                const ImVec2 point(float(x) + .5f, float(y) + .5f);
                float e[3] = {Edge(p[1], p[2], point), Edge(p[2], p[0], point), Edge(p[0], p[1], point)};
                if (!Inside(e[0], p[1], p[2]) || !Inside(e[1], p[2], p[0]) || !Inside(e[2], p[0], p[1])) continue;
                for (float& weight : e) weight /= area;
                const ImVec2 uv(v[0]->uv.x * e[0] + v[1]->uv.x * e[1] + v[2]->uv.x * e[2],
                    v[0]->uv.y * e[0] + v[1]->uv.y * e[1] + v[2]->uv.y * e[2]);
                const auto color = [&](int channel) {
                    return float((v[0]->col >> (channel * 8)) & 255) * e[0] + float((v[1]->col >> (channel * 8)) & 255) * e[1] + float((v[2]->col >> (channel * 8)) & 255) * e[2];
                };
                const float alpha = texture(uv, 3) * color(3) / (255.f * 255.f);
                auto* pixel = &pixels[(size_t(y) * width + x) * 4];
                for (int c = 0; c < 3; ++c)
                    pixel[c] = uint8_t(std::clamp(std::lround(texture(uv, c) * color(c) / 255.f * alpha + pixel[c] * (1 - alpha)), 0l, 255l));
            }
        }
    }
    SavePng(path, width, height, pixels);
}
