#pragma once

#include <mbgl/text/glyph.hpp>
#include <mbgl/text/glyph_atlas_observer.hpp>
#include <mbgl/text/glyph_range.hpp>
#include <mbgl/text/glyph_set.hpp>
#include <mbgl/geometry/binpack.hpp>
#include <mbgl/util/noncopyable.hpp>
#include <mbgl/util/optional.hpp>
#include <mbgl/util/font_stack.hpp>
#include <mbgl/util/exclusive.hpp>
#include <mbgl/util/work_queue.hpp>
#include <mbgl/util/image.hpp>
#include <mbgl/gl/texture.hpp>
#include <mbgl/gl/object.hpp>

#include <atomic>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <mutex>

namespace mbgl {

class FileSource;
class GlyphPBF;

namespace gl {
class Context;
} // namespace gl

class GlyphRequestor {
public:
    virtual void onGlyphsAvailable(GlyphPositionMap) = 0;
};
    
class GlyphAtlas : public util::noncopyable, public GlyphAtlasObserver {
public:
    GlyphAtlas(Size, FileSource&);
    ~GlyphAtlas();

    GlyphSet& getGlyphSet(const FontStack&);
    
    // Workers send a `getGlyphs` message to the main thread once they have determined
    // which glyphs they will need. Invoking this method will increment reference
    // counts for all the glyphs in `GlyphDependencies`. If all glyphs are already
    // locally available, the observer will be notified that the glyphs are available
    // immediately. Otherwise, a request on the FileSource is made, and when all glyphs
    // are parsed and added to the atlas, the observer will be notified.
    void getGlyphs(uintptr_t tileUID, GlyphDependencies glyphs, GlyphRequestor& requestor);

    void setURL(const std::string &url) {
        glyphURL = url;
    }

    std::string getURL() const {
        return glyphURL;
    }

    void setObserver(GlyphAtlasObserver* observer);

    void removeGlyphs(uintptr_t tileUID);

    // Binds the atlas texture to the GPU, and uploads data if it is out of date.
    void bind(gl::Context&, gl::TextureUnit unit);

    // Uploads the texture to the GPU to be available when we need it. This is a lazy operation;
    // the texture is only bound when the data is out of date (=dirty).
    void upload(gl::Context&, gl::TextureUnit unit);

    Size getSize() const;
    
    virtual void onGlyphsLoaded(const FontStack&, const GlyphRange&);
    virtual void onGlyphsError(const FontStack&, const GlyphRange&, std::exception_ptr);
    
    // TODO: Only exposed for tests, maybe do this some other way?
    bool hasGlyphRanges(const FontStack&, const GlyphRangeSet& ranges) const;
    // TODO: Only made public for tests
    void addGlyphs(uintptr_t tileUID, const GlyphDependencies& glyphDependencies);
    // Workers are given a copied 'GlyphPositions' map to use for placing their glyphs.
    // The positions specified in this object are guaranteed to be
    // valid for the lifetime of the tile.
    GlyphPositionMap getGlyphPositions(const GlyphDependencies& glyphs) const;
    
private:
    bool hasGlyphRange(const FontStack&, const GlyphRange& range) const;
    
    void addGlyph(uintptr_t tileUID, const FontStack&, const SDFGlyph&);
    
    FileSource& fileSource;
    std::string glyphURL;

    struct GlyphValue {
        GlyphValue(Rect<uint16_t> rect_, uintptr_t id)
            : rect(std::move(rect_)), ids({ id }) {}
        Rect<uint16_t> rect;
        std::unordered_set<uintptr_t> ids;
    };

    struct Entry {
        std::map<GlyphRange, GlyphPBF> ranges;
        GlyphSet glyphSet;
        std::map<uint32_t, GlyphValue> glyphValues;
    };

    std::unordered_map<FontStack, Entry, FontStackHash> entries;
    
    struct TileDependency {
        TileDependency(const GlyphRangeDependencies& _pendingRanges, GlyphDependencies _glyphDependencies, GlyphRequestor* _requestor)
            : pendingRanges(_pendingRanges), glyphDependencies(std::move(_glyphDependencies)), requestor(_requestor)
        {}
        GlyphRangeDependencies pendingRanges;
        GlyphDependencies glyphDependencies;
        GlyphRequestor* requestor;
    };
    std::unordered_map<uintptr_t,TileDependency> tileDependencies;
    typedef std::pair<FontStack,GlyphRange> PendingGlyphRange;
    std::map<PendingGlyphRange, std::set<uintptr_t>> pendingGlyphRanges;

    GlyphAtlasObserver* observer = nullptr;

    BinPack<uint16_t> bin;
    AlphaImage image;
    std::atomic<bool> dirty;
    mbgl::optional<gl::Texture> texture;
};

} // namespace mbgl
