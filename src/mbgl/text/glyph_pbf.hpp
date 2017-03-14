#pragma once

#include <mbgl/text/glyph.hpp>
#include <mbgl/util/font_stack.hpp>
#include <mbgl/util/noncopyable.hpp>

#include <atomic>
#include <functional>
#include <string>
#include <memory>

namespace mbgl {

class GlyphAtlas;
class GlyphRequestor;
class GlyphAtlasObserver;
class AsyncRequest;
class FileSource;

class GlyphPBF : private util::noncopyable {
public:
    GlyphPBF(GlyphAtlas*,
             const FontStack&,
             const GlyphRange&,
             GlyphAtlasObserver*,
             FileSource&);
    ~GlyphPBF();

    void addRequestor(GlyphRequestor&);
    std::set<GlyphRequestor*> processRequestors();
    
    bool isParsed() const {
        return parsed;
    }

private:
    bool parsed;
    std::unique_ptr<AsyncRequest> req;
    GlyphAtlasObserver* observer = nullptr;
    std::set<GlyphRequestor*> requestors;
};

} // namespace mbgl
