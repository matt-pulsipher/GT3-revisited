#include "render/prize_view.h"

#include "base/log.h"

#include <cmath>

namespace gt3r::render {

bool PrizeView::load(ByteSpan prm_bytes, const std::string& name) {
    file_.reset();
    try {
        if (!prm::PrmFile::is_prm(prm_bytes)) return false;
        bytes_.assign(prm_bytes.begin(), prm_bytes.end());
        file_ = prm::PrmFile::parse(bytes_);
        model_ = prm::load_prm_model(bytes_, {}, name);
    } catch (const std::exception& e) {
        LOG_WARN("Prize %s: %s", name.c_str(), e.what());
        file_.reset();
        return false;
    }
    gpu_ = std::make_unique<GpuModel>(ctx_, model_); // once: the sphere map is applied on the GPU
    shadow_.reset();
    try {
        scene::Model sh = prm::load_prm_shadow(bytes_, name);
        if (!sh.meshes.empty()) shadow_ = std::make_unique<GpuModel>(ctx_, sh);
    } catch (const std::exception&) {
    }
    restart();
    return true;
}

void PrizeView::restart() {
    t_ = file_ ? file_->start_frame : 0;
    acc_ = 0;
}

void PrizeView::tick(double seconds) {
    if (!file_) return;
    acc_ += seconds * 60.0;
    while (acc_ >= 1.0) {
        t_ = file_->advance(t_);
        acc_ -= 1.0;
    }
}

void PrizeView::draw(SDL_Renderer* r, const SDL_FRect& rect, const float clear[4], const Pass* pass) {
    if (!file_ || rect.w <= 0 || rect.h <= 0) return;
    prm::Matrix mv = prm::multiply(file_->model_at(t_), file_->view_at(t_));
    if (!view_) view_ = std::make_unique<View3D>(ctx_);
    DrawOptions o;
    o.lod = -1;
    if (clear)
        for (int i = 0; i < 4; ++i) o.clear[i] = clear[i];
    o.composite_alpha = alpha_;
    o.repeated = pass && pass->repeated;
    // The scene's lights (prm.md "Lights"), set like OpenGL lights after the view matrix:
    // directional, towards the light = the record's vector normalised and turned by the view
    // (camera space here, where the model is drawn). Light-model ambient (0, 0, 0); each
    // light adds its ambient x Ma. Materials are lit with the VU formula (scene::Material).
    o.gt3_lighting = true;
    {
        const prm::Matrix vw = file_->view_at(t_);
        for (float& a : o.light_ambient) a = 0;
        int n = 0;
        for (const prm::Light& l : file_->lights) {
            if (n >= 4) break;
            float d[3];
            for (int j = 0; j < 3; ++j)
                d[j] = l.direction[0] * vw[j] + l.direction[1] * vw[4 + j] + l.direction[2] * vw[8 + j];
            float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (len < 1e-6f) continue;
            for (float& v : d) v /= len;
            for (int i = 0; i < 3; ++i) o.light_ambient[i] += l.ambient[size_t(i)];
            if (n == 0) {
                o.light_dir = {d[0], d[1], d[2]};
                for (int i = 0; i < 3; ++i) o.light_diffuse[i] = l.diffuse[size_t(i)], o.light_specular[i] = l.specular[size_t(i)];
            } else {
                for (int i = 0; i < 3; ++i) {
                    o.fill_dir[n - 1][i] = d[i];
                    o.fill_diffuse[n - 1][i] = l.diffuse[size_t(i)], o.fill_specular[n - 1][i] = l.specular[size_t(i)];
                }
                o.fill_count = n;
            }
            ++n;
        }
        if (n == 0)
            for (float& c : o.light_diffuse) c = 0;
    }
    // The game's camera: 60 degrees horizontally over the 640x448 frame.
    float aspect = pass && pass->aspect > 0 ? pass->aspect : rect.w / rect.h;
    float fovy = 2.0f * std::atan(std::tan(30.0f * 3.14159265f / 180.0f) / aspect);
    Mat4 proj = perspective(fovy, aspect, 0.1f, 300.0f);
    Mat4 view = Mat4::identity();
    if (pass) {
        // Eye moved by (ex, ey); the frustum shifted so the plane at `focus` stays put; then
        // the sub-pixel shift.
        view = translate({-pass->ex, -pass->ey, 0});
        float sx = 0, sy = 0;
        if (pass->focus > 0) sx = pass->ex / pass->focus * proj.at(0, 0), sy = pass->ey / pass->focus * proj.at(1, 1);
        sx += 2.0f * pass->px / rect.w;
        sy -= 2.0f * pass->py / rect.h;
        // NDC offset: x' = x + s w (column 2 picks up -z = w for this projection).
        proj.at(0, 2) -= sx;
        proj.at(1, 2) -= sy;
    }
    std::vector<DrawItem> items;
    // The shadow node (license cards): flattened onto the floor along light 0, drawn first.
    if (shadow_ && !file_->lights.empty()) {
        prm::Matrix S;
        if (prm::shadow_matrix(file_->lights[0], 0, S)) {
            DrawItem sh;
            sh.model = shadow_.get();
            sh.transform = Mat4::from(prm::multiply(prm::multiply(file_->model_at(t_), S), file_->view_at(t_)));
            items.push_back(sh);
        }
    }
    DrawItem it;
    it.model = gpu_.get();
    it.transform = Mat4::from(mv); // row-vector storage = the column-vector matrix
    it.sphere_map = true;          // textured environment-map primitives: view-space sphere map
    items.push_back(it);
    view_->draw(r, rect, view, proj, Vec3{pass ? pass->ex : 0, pass ? pass->ey : 0, 0}, items, o);
}

} // namespace gt3r::render
