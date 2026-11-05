#include "menu/menu_canvas.h"

#include "menu/menu_scene.h"

namespace gt3r::menu {

void MenuCanvas::release() {
    if (picture_) SDL_DestroyTexture(picture_);
    if (layer_) SDL_DestroyTexture(layer_);
    picture_ = layer_ = shown_ = nullptr;
    w_ = h_ = 0;
}

void MenuCanvas::begin(SDL_Renderer* r, SDL_Texture* page, int w, int h) {
    page_ = page;
    shown_ = nullptr;
    drawn_ = false;
    if (w <= int(kMenuWidth) || h <= int(kMenuHeight)) w = h = 0;
    if (w != w_ || h != h_) release();
    if (w <= 0) return;
    if (!picture_) {
        picture_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
        // Shown pixel for pixel, also where the screen area starts between two pixels.
        if (picture_) SDL_SetTextureScaleMode(picture_, SDL_SCALEMODE_NEAREST);
    }
    if (!layer_) {
        layer_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, int(kMenuWidth), int(kMenuHeight));
        if (layer_) {
            SDL_SetTextureScaleMode(layer_, SDL_SCALEMODE_LINEAR);
            SDL_SetTextureBlendMode(layer_, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
        }
    }
    if (!picture_ || !layer_) {
        release();
        return;
    }
    w_ = w, h_ = h;
}

void MenuCanvas::end(SDL_Renderer* r) {
    if (!drawn_) return;
    SDL_SetRenderTarget(r, picture_);
    SDL_RenderTexture(r, layer_, nullptr, nullptr);
    shown_ = picture_;
    SDL_SetRenderTarget(r, page_);
    SDL_SetRenderClipRect(r, nullptr); // (MenuRenderer's: it ended on the layer)
}

void MenuCanvas::model_begin(SDL_Renderer* r) {
    SDL_Texture* art = drawn_ ? layer_ : page_;
    SDL_SetRenderTarget(r, picture_);
    // (An explicit clip: SDL's GPU renderer can keep the scissor of a smaller target.)
    const SDL_Rect whole{0, 0, w_, h_};
    SDL_SetRenderClipRect(r, &whole);
    SDL_RenderTexture(r, art, nullptr, nullptr);
    drawn_ = true;
}

void MenuCanvas::model_end(SDL_Renderer* r) {
    SDL_SetRenderTarget(r, layer_);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);
}

SDL_FRect MenuCanvas::to_picture(const SDL_FRect& box) const {
    const float sx = scale_x(), sy = scale_y();
    return {box.x * sx, box.y * sy, box.w * sx, box.h * sy};
}

} // namespace gt3r::menu
