#include "DisplayPresenter.h"

#include "ImgItem.h"

DisplayPresentationResult DisplayPresenter::Present(const DisplaySnapshot& snapshot,
                                                    const DisplayPresentationInput& input) const
{
    if (snapshot.item == nullptr)
    {
        return {};
    }

    const auto presentation = DecidePresentation(snapshot.state.status, snapshot.state.frame != nullptr,
                                                 input.waiting_for_image != FALSE, input.first_paint != FALSE);
    if (presentation != DisplayPresentation::ImageReady)
    {
        return {presentation, {}};
    }

    const auto bitmap = snapshot.state.frame->GetBitmap();
    const ImgRenderInput render_input{input.dc,
                                      input.background_brush,
                                      input.client_rectangle,
                                      bitmap.bitmap(),
                                      snapshot.state.frame->offsetx(),
                                      snapshot.state.frame->offsety(),
                                      snapshot.state.frame->width(),
                                      snapshot.state.frame->height(),
                                      input.has_protected_rectangle,
                                      input.protected_rectangle};
    const auto render_result = renderer_.Render(render_input);
    return {render_result.Succeeded() ? DisplayPresentation::ImageReady : DisplayPresentation::RenderFailed,
            render_result};
}
