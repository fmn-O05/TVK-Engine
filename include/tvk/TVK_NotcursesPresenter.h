#pragma once

#ifdef TVK_NOTCURSES_AVAILABLE

#include "tvk/TVK_TerminalPresenter.h"
#include <cstdint>
#include <vector>

struct notcurses;
struct ncplane;

namespace tvk {

class NotcursesPresenter : public IPresenter {
public:
    explicit NotcursesPresenter(bool headless = false);
    ~NotcursesPresenter() override;

    void ResizeBuffers(int new_w, int new_h) override;
    bool TryUpdateTerminalSize(int fallback_w, int fallback_h) override;

    void Present(const std::vector<Pixel>& framebuffer,
                 const std::vector<float>& zbuffer,
                 int pix_w, int term_w, int term_h) override;

    void InvalidateFrontBuffer() override {}

    BlitterType GetActiveBlitter() const override { return BlitterType::BrailleCustom; }

private:
    bool m_Headless;
    notcurses* m_NC;
    ncplane* m_StdPlane;
    ncplane* m_HudPlane;

    int m_TermW;
    int m_TermH;
};

}

#endif
