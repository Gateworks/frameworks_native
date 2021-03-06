/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <math.h>

#include <GLES/gl.h>
#include <GLES/glext.h>

#include <utils/Errors.h>
#include <utils/Log.h>

#include <ui/GraphicBuffer.h>

#include "LayerDim.h"
#include "SurfaceFlinger.h"
#include "DisplayDevice.h"

namespace android {
// ---------------------------------------------------------------------------

LayerDim::LayerDim(SurfaceFlinger* flinger, const sp<Client>& client,
        const String8& name, uint32_t w, uint32_t h, uint32_t flags)
    : Layer(flinger, client, name, w, h, flags) {
}

LayerDim::~LayerDim() {
}

void LayerDim::onDraw(const sp<const DisplayDevice>& hw, const Region& clip) const
{
    const State& s(drawingState());
    if (s.alpha>0) {
        const GLfloat alpha = s.alpha/255.0f;
        const uint32_t fbHeight = hw->getHeight();
        glDisable(GL_TEXTURE_EXTERNAL_OES);
        glDisable(GL_TEXTURE_2D);

        if (s.alpha == 0xFF) {
            glDisable(GL_BLEND);
        } else {
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        }

        glColor4f(0, 0, 0, alpha);

        LayerMesh mesh;
        computeGeometry(hw, &mesh);

        glVertexPointer(2, GL_FLOAT, 0, mesh.getVertices());
        glDrawArrays(GL_TRIANGLE_FAN, 0, mesh.getVertexCount());

        glDisable(GL_BLEND);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }
}

bool LayerDim::isVisible() const {
    const Layer::State& s(drawingState());
    return !(s.flags & layer_state_t::eLayerHidden) && s.alpha;
}

Rect LayerDim::computeCrop(const sp<const DisplayDevice>& hw) const {
    /*
     * The way we compute the crop (aka. texture coordinates when we have a
     * Layer) produces a different output from the GL code in
     * drawWithOpenGL() due to HWC being limited to integers. The difference
     * can be large if getContentTransform() contains a large scale factor.
     * See comments in drawWithOpenGL() for more details.
     */

    // the content crop is the area of the content that gets scaled to the
    // layer's size.
    Rect crop(getContentCrop());

    // the active.crop is the area of the window that gets cropped, but not
    // scaled in any ways.
    const State& s(drawingState());

    // apply the projection's clipping to the window crop in
    // layerstack space, and convert-back to layer space.
    // if there are no window scaling (or content scaling) involved,
    // this operation will map to full pixels in the buffer.
    // NOTE: should we revert to GL composition if a scaling is involved
    // since it cannot be represented in the HWC API?
    Rect activeCrop(s.transform.transform(s.active.crop));
    activeCrop.intersect(hw->getViewport(), &activeCrop);
    activeCrop = s.transform.inverse().transform(activeCrop);

    // paranoia: make sure the window-crop is constrained in the
    // window's bounds
    activeCrop.intersect(Rect(s.active.w, s.active.h), &activeCrop);

    if (!activeCrop.isEmpty()) {
        // Transform the window crop to match the buffer coordinate system,
        // which means using the inverse of the current transform set on the
        // SurfaceFlingerConsumer.
        uint32_t invTransform = getContentTransform();
        int winWidth = s.active.w;
        int winHeight = s.active.h;
        if (invTransform & NATIVE_WINDOW_TRANSFORM_ROT_90) {
            invTransform ^= NATIVE_WINDOW_TRANSFORM_FLIP_V |
                    NATIVE_WINDOW_TRANSFORM_FLIP_H;
            winWidth = s.active.h;
            winHeight = s.active.w;
        }
        const Rect winCrop = activeCrop.transform(
                invTransform, s.active.w, s.active.h);

        // the code below essentially performs a scaled intersection
        // of crop and winCrop
        float xScale = float(crop.width()) / float(winWidth);
        float yScale = float(crop.height()) / float(winHeight);

        int insetL = int(ceilf( winCrop.left                * xScale));
        int insetT = int(ceilf( winCrop.top                 * yScale));
        int insetR = int(ceilf((winWidth  - winCrop.right ) * xScale));
        int insetB = int(ceilf((winHeight - winCrop.bottom) * yScale));

        crop.left   += insetL;
        crop.top    += insetT;
        crop.right  -= insetR;
        crop.bottom -= insetB;
    }
    return crop;
}


void LayerDim::setGeometry(
    const sp<const DisplayDevice>& hw,
        HWComposer::HWCLayerInterface& layer)
{
    layer.setDefaultState();

    // enable this layer
    layer.setSkip(false);

    // blending mode
    layer.setBlending(HWC_BLENDING_DIM);

    // set dim value
    const State& s(drawingState());
    layer.setPlaneAlpha(s.alpha);

    if (isSecure() && !hw->isSecure()) {
        layer.setSkip(true);
    }
    // apply the layer's transform, followed by the display's global transform
    // here we're guaranteed that the layer's transform preserves rects
    Rect frame(s.transform.transform(computeBounds()));
    frame.intersect(hw->getViewport(), &frame);
    const Transform& tr(hw->getTransform());
    layer.setFrame(tr.transform(frame));
    layer.setCrop(computeCrop(hw));
}

void LayerDim::setPerFrameData(const sp<const DisplayDevice>& hw,
        HWComposer::HWCLayerInterface& layer) {
    // we have to set the visible region on every frame because
    // we currently free it during onLayerDisplayed(), which is called
    // after HWComposer::commit() -- every frame.
    // Apply this display's projection's viewport to the visible region
    // before giving it to the HWC HAL.
    const Transform& tr = hw->getTransform();
    Region visible = tr.transform(visibleRegion.intersect(hw->getViewport()));
    layer.setVisibleRegionScreen(visible);
    // leave it as it is for dim layer.
}

// ---------------------------------------------------------------------------

}; // namespace android
