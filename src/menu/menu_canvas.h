#pragma once

#include <SDL3/SDL.h>

namespace gt3r::menu {

// A menu page with its 3D models at the screen's resolution.
//
// The art stays the 640 x 448 picture the pages are made for. A model goes into a larger
// picture: the art drawn before it, scaled up, under it and the art drawn after it, scaled up
// the same way, over it. Elements blend or add, which a layer started at transparent black
// records exactly: its colour is what they put on the picture, 1 - its alpha what they keep
// of it. A page with no model stays the 640 x 448 picture.
class MenuCanvas {
public:
    MenuCanvas() = default;
    MenuCanvas(const MenuCanvas&) = delete;
    MenuCanvas& operator=(const MenuCanvas&) = delete;
    ~MenuCanvas() { release(); }

    // Starts a page drawn into `page` (the render target). `w` x `h`: the page on the screen in
    // pixels; when that is no larger than the page, the models stay in it (on() is false).
    void begin(SDL_Renderer* r, SDL_Texture* page, int w, int h);
    // Ends the page; the render target is `page` again.
    void end(SDL_Renderer* r);
    // Models go into the picture.
    bool on() const { return w_ > 0; }

    // Around a model: the art so far goes into the picture, the render target until model_end.
    // Then the target is the layer for the art after it.
    void model_begin(SDL_Renderer* r);
    void model_end(SDL_Renderer* r);

    // Picture pixels per menu pixel. (Not square: the page is 640 x 448 on a 4:3 screen area.)
    float scale_x() const { return float(w_) / 640.0f; }
    float scale_y() const { return float(h_) / 448.0f; }
    SDL_FRect to_picture(const SDL_FRect& box) const;

    // What to show of the last page: the picture, or `page` when it had no model.
    SDL_Texture* shown(SDL_Texture* page) const { return shown_ ? shown_ : page; }

    void release();

private:
    SDL_Texture* page_ = nullptr;    // the page: the art under the first model
    SDL_Texture* picture_ = nullptr; // on-screen size
    SDL_Texture* layer_ = nullptr;   // 640 x 448: the art since the last model
    SDL_Texture* shown_ = nullptr;
    int w_ = 0, h_ = 0;
    bool drawn_ = false; // a model of this page is in the picture
};

} // namespace gt3r::menu
