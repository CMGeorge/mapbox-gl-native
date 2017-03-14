#include <mbgl/text/glyph_atlas.hpp>
#include <mbgl/text/glyph_atlas_observer.hpp>
#include <mbgl/text/glyph_pbf.hpp>
#include <mbgl/gl/context.hpp>
#include <mbgl/util/logging.hpp>
#include <mbgl/util/platform.hpp>

#include <cassert>
#include <algorithm>

namespace mbgl {

static GlyphAtlasObserver nullObserver;

GlyphAtlas::GlyphAtlas(const Size size, FileSource& fileSource_)
    : fileSource(fileSource_),
      observer(&nullObserver),
      bin(size.width, size.height),
      image(size),
      dirty(true) {
}

GlyphAtlas::~GlyphAtlas() = default;

GlyphSet& GlyphAtlas::getGlyphSet(const FontStack& fontStack) {
    return entries[fontStack].glyphSet;
}
    
bool GlyphAtlas::hasGlyphRanges(const FontStack& fontStack, const GlyphRangeSet& ranges) const {
    for (const auto& range : ranges) {
        if (!hasGlyphRange(fontStack,range)) {
            return false;
        }
    }
    return true;
}
    
bool GlyphAtlas::hasGlyphRange(const FontStack& fontStack, const GlyphRange& range) const {
    auto entry = entries.find(fontStack);
    if (entry == entries.end())
        return false;
    
    auto rangeIt = entry->second.ranges.find(range);
    if (rangeIt == entry->second.ranges.end())
        return false;
    
    return rangeIt->second.isParsed();
}
    
void GlyphAtlas::getGlyphs(GlyphRequestor& requestor, GlyphDependencies glyphDependencies) {
    // Figure out which glyph ranges need to be fetched
    GlyphRangeDependencies missing;
    for (const auto& fontStack : glyphDependencies) {
        for (auto glyphID : fontStack.second) {
            GlyphRange range = getGlyphRange(glyphID);
            if (!hasGlyphRange(fontStack.first,range)) {
                missing[fontStack.first].insert(range);
            }
        }
    }
    
    if (missing.empty()) {
        // Send "glyphs available" message immediately to requestor
        addGlyphs(requestor, glyphDependencies);
        requestor.onGlyphsAvailable(getGlyphPositions(glyphDependencies));
    } else {
        // Trigger network requests for glyphs, send "glyphs available" message when all requests have finished
        tileDependencies.emplace(std::piecewise_construct,
                                 std::forward_as_tuple(&requestor),
                                 std::forward_as_tuple(missing, std::move(glyphDependencies)));
        for (auto fontStack : missing) {
            for (auto& range : fontStack.second) {
                pendingGlyphRanges[PendingGlyphRange(fontStack.first,range)].insert(&requestor);
                
                entries[fontStack.first].ranges.emplace(std::piecewise_construct,
                               std::forward_as_tuple(range),
                               std::forward_as_tuple(this, fontStack.first, range, this, fileSource));
            }
        }
    }
}

void GlyphAtlas::setObserver(GlyphAtlasObserver* observer_) {
    observer = observer_;
}
    
void GlyphAtlas::onGlyphsError(const FontStack& fontStack, const GlyphRange& range, std::exception_ptr err) {
    if (observer) {
        observer->onGlyphsError(fontStack, range, err);
    }
}

void GlyphAtlas::onGlyphsLoaded(const FontStack& fontStack, const GlyphRange& range) {
    auto dependendentTiles = pendingGlyphRanges.find(PendingGlyphRange(fontStack,range));
    if (dependendentTiles != pendingGlyphRanges.end()) {
        for (auto requestor : dependendentTiles->second) {
            auto tileDependency = tileDependencies.find(requestor);
            if (tileDependency != tileDependencies.end()) {
                auto fontRanges = tileDependency->second.pendingRanges.find(fontStack);
                if (fontRanges != tileDependency->second.pendingRanges.end()) {
                    fontRanges->second.erase(range);
                    if (fontRanges->second.empty()) {
                        tileDependency->second.pendingRanges.erase(fontRanges);
                    }
                }
                if (tileDependency->second.pendingRanges.empty()) {
                    addGlyphs(*requestor, tileDependency->second.glyphDependencies);
                    tileDependency->first->onGlyphsAvailable(getGlyphPositions(tileDependency->second.glyphDependencies));
                    tileDependencies.erase(requestor);
                }
            }
        }
        pendingGlyphRanges.erase(PendingGlyphRange(fontStack,range));
    }
    if (observer) {
        observer->onGlyphsLoaded(fontStack, range);
    }
}
    

// Builds up the set of glyph positions needed for a given tile; result is handed to worker threads
GlyphPositionMap GlyphAtlas::getGlyphPositions(const GlyphDependencies& glyphDependencies) const {
    GlyphPositionMap glyphPositions;

    for (const auto& fontStack : glyphDependencies) {
        auto entry = entries.find(fontStack.first);
        if (entry != entries.end()) {

            for (GlyphID glyphID : fontStack.second) {
                auto sdf = entry->second.glyphSet.getSDFs().find(glyphID);
                if (sdf != entry->second.glyphSet.getSDFs().end()) {
                    auto glyphRect = entry->second.glyphValues.find(glyphID);
                    // It's possible to have an SDF without a valid position (if the SDF was malformed). We indicate this case with Rect<uint16_t>(0,0,0,0).
                    const Rect<uint16_t> rect = glyphRect == entry->second.glyphValues.end() ? Rect<uint16_t>(0,0,0,0) : glyphRect->second.rect;
                    glyphPositions[fontStack.first].emplace(std::piecewise_construct,
                                                            std::forward_as_tuple(glyphID),
                                                            std::forward_as_tuple(rect, sdf->second.metrics));
                }
            }
        }
    }
    return glyphPositions;
}
    
void GlyphAtlas::addGlyphs(GlyphRequestor& requestor, const GlyphDependencies &glyphDependencies) {
    for (const auto& fontStack : glyphDependencies) {
        const auto& sdfs = getGlyphSet(fontStack.first).getSDFs();
        for (auto glyphID : fontStack.second) {
            auto it = sdfs.find(glyphID);
            if (it != sdfs.end()) { // If we got the range, but still didn't get a glyph, go ahead with rendering
                addGlyph(requestor, fontStack.first, it->second);
            }
        }
    }
}

void GlyphAtlas::addGlyph(GlyphRequestor& requestor,
                          const FontStack& fontStack,
                          const SDFGlyph& glyph)
{
    std::map<uint32_t, GlyphValue>& face = entries[fontStack].glyphValues;
    auto it = face.find(glyph.id);

    // The glyph is already in this texture.
    if (it != face.end()) {
        GlyphValue& value = it->second;
        value.ids.insert(&requestor);
        return;
    }

    // Guard against glyphs that are too large, or that we don't need to place into the atlas since
    // they don't have any pixels.
    if (glyph.metrics.width == 0 || glyph.metrics.width >= 256 ||
        glyph.metrics.height == 0 || glyph.metrics.height >= 256) {
        return;
    }

    // Add a 1px border around every image.
    const uint32_t padding = 1;
    uint16_t width = glyph.bitmap.size.width + 2 * padding;
    uint16_t height = glyph.bitmap.size.height + 2 * padding;

    // Increase to next number divisible by 4, but at least 1.
    // This is so we can scale down the texture coordinates and pack them
    // into 2 bytes rather than 4 bytes.
    width += (4 - width % 4);
    height += (4 - height % 4);

    Rect<uint16_t> rect = bin.allocate(width, height);
    if (rect.w == 0) {
        Log::Error(Event::OpenGL, "glyph bitmap overflow");
        return;
    }

    face.emplace(glyph.id, GlyphValue { rect, &requestor });

    AlphaImage::copy(glyph.bitmap, image, { 0, 0 }, { rect.x + padding, rect.y + padding }, glyph.bitmap.size);

    dirty = true;
}

void GlyphAtlas::removeGlyphs(GlyphRequestor& requestor) {
    for (auto& entry : entries) {
        std::map<uint32_t, GlyphValue>& face = entry.second.glyphValues;
        for (auto it = face.begin(); it != face.end(); /* we advance in the body */) {
            GlyphValue& value = it->second;
            value.ids.erase(&requestor);

            if (value.ids.empty()) {
                const Rect<uint16_t>& rect = value.rect;

                // Clear out the bitmap.
                uint8_t *target = image.data.get();
                for (uint32_t y = 0; y < rect.h; y++) {
                    uint32_t y1 = image.size.width * (rect.y + y) + rect.x;
                    for (uint32_t x = 0; x < rect.w; x++) {
                        target[y1 + x] = 0;
                    }
                }

                bin.release(rect);

                // Make sure to post-increment the iterator: This will return the
                // current iterator, but will go to the next position before we
                // erase the element from the map. That way, the iterator stays
                // valid.
                face.erase(it++);
            } else {
                ++it;
            }
        }
    }
}

Size GlyphAtlas::getSize() const {
    return image.size;
}

void GlyphAtlas::upload(gl::Context& context, gl::TextureUnit unit) {
    if (!texture) {
        texture = context.createTexture(image, unit);
    } else if (dirty) {
        context.updateTexture(*texture, image, unit);
    }
    
    dirty = false;
}

void GlyphAtlas::bind(gl::Context& context, gl::TextureUnit unit) {
    upload(context, unit);
    context.bindTexture(*texture, unit, gl::TextureFilter::Linear);
}

} // namespace mbgl
