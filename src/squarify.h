#ifndef _SQUARIFY_HPP_
#define _SQUARIFY_HPP_

#include <vector>
#include <numeric>
#include <algorithm>
#include <cassert>
#include <memory>

namespace Squarify {

struct Rect
{
    Rect() : x(0.0f), y(0.0f), width(0.0f), height(0.0f) {}
    Rect(const Rect&) = default;
    Rect(float _x, float _y, float _w, float _h) : x(_x), y(_y), width(_w), height(_h) {}

    float x;
    float y;
    float width;
    float height;
};

struct TreeMapNode
{
    struct Element
    {
        size_t index;
        Rect rect;
    };

    TreeMapNode() = default;
    TreeMapNode(TreeMapNode&& o) = default;
    ~TreeMapNode() = default;
    TreeMapNode& operator=(TreeMapNode&& o) = default;

    // would result in undefined 'next' ownership
    TreeMapNode(const TreeMapNode &) = delete;
    TreeMapNode &operator=(const TreeMapNode&) = delete;

    /**
     * The area encapsulated by both the elements in this hierarchy level,
     * plus all the child nodes
     */
    Rect bounds;

    /**
     * Elements included in this hierarchy level
     */
    std::vector<Element> elements;

    /**
     * If non-null: Further subdivision of more elements.
     * Is owned by this TreeMapNode and will be de-allocated on destruction.
     */
    std::unique_ptr<TreeMapNode> next;
};

class Squarify
{
private:
    struct Element {
        size_t originalIndex;
        float size;
    };
    std::vector<Element> m_elements;

    // to avoid unnecessary allocations/deallocations during operation
    mutable std::vector<Rect> m_rectBuf;

    Rect m_rect;

    static float aspectRatio(const Rect &rect)
    {
        return (rect.width > rect.height)
                ? (rect.width / rect.height)
                : (rect.height / rect.width);
    }

    /** Denotes a range [from, to[ in m_elements */
    struct Span
    {
        size_t begin;
        size_t end;
        size_t len() const { return end - begin; }
    };

    float sum(Span span) const
    {
        float sum = 0.0f;
        for (size_t i = span.begin; i < span.end; ++i)
            sum += m_elements[i].size;
        return sum;
    }

    /**
     * If the given rectangle is horizontal, layout the given sizes vertically.
     * If the given rectangle is vertical, layout the given sizes horizontally.
     */
    void layout(std::vector<Rect> &dst, Span span, const Rect &rect) const
    {
        dst.clear();
        const float area = sum(span);

        if (rect.width < rect.height) {
            float height = area / rect.width;
            float x = rect.x;
            for (size_t i = span.begin; i < span.end; ++i) {
                dst.push_back(Rect{x, rect.y, m_elements[i].size / height, height});
                x += m_elements[i].size / height;
            }
        }
        else {
            float width = area / rect.height;
            float y = rect.y;
            for (size_t i = span.begin; i < span.end; ++i) {
                dst.push_back(Rect{rect.x, y, width, m_elements[i].size / width});
                y += m_elements[i].size / width;
            }
        }
    }

    /** Cut off the amount of size from the longest edge of the rect */
    Rect leftover(Span span, const Rect &rect) const
    {
        const float area = sum(span);

        if (rect.width < rect.height) {
            float height = area / rect.width;
            return Rect{rect.x, rect.y + height, rect.width, rect.height - height};
        }
        else {
            float width = area / rect.height;
            return Rect{rect.x + width, rect.y, rect.width - width, rect.height};
        }
    }

    float worstRatio(std::vector<Rect> &dst, Span span, const Rect &rect) const
    {
        assert(span.len() > 0);
        layout(dst, span, rect);
        float worst = aspectRatio(dst[0]);
        for (size_t i = 1; i < dst.size(); ++i)
            worst = std::max(worst, aspectRatio(dst[1]));
        return worst;
    }

    TreeMapNode squarify(Span s, const Rect &rect) const
    {
        assert(s.len() > 0);

        TreeMapNode ret;
        ret.bounds = rect;

        size_t split = s.begin + 1;
        float ratio = worstRatio(m_rectBuf, Span{s.begin, split}, rect);
        while (split < s.end) {
            const float newRatio = worstRatio(m_rectBuf, Span{s.begin, split + 1}, rect);
            if (ratio >= newRatio) {
                ratio = newRatio;
                ++split;
            }
            else
                break;
        }

        const Span current = Span{s.begin, split};
        const Span remaining = Span{split, s.end};
        const Rect left = leftover(current, rect);

        layout(m_rectBuf, current, rect);
        assert(m_rectBuf.size() == current.len());
        ret.elements.reserve(m_rectBuf.size());
        for (size_t i = 0; i < m_rectBuf.size(); ++i) {
            ret.elements.push_back({m_elements[current.begin + i].originalIndex, m_rectBuf[i]});
        }

        if (remaining.len() > 0) {
            ret.next.reset(new TreeMapNode());
            *ret.next = squarify(remaining, left);
        }

        return ret;
    }

    static void gather(std::vector<TreeMapNode::Element> &dst, const TreeMapNode &node)
    {
        for (const TreeMapNode::Element &e : node.elements)
            dst.push_back(e);
        if (node.next)
            gather(dst, *node.next);
    }

public:
    Squarify(const std::vector<float> &sizes, const Rect &rect)
        : m_rect(rect)
    {
        const float sum = std::accumulate(sizes.begin(), sizes.end(), 0.0f);
        const float area = rect.width * rect.height;

        m_elements.reserve(sizes.size());
        for (size_t i = 0; i < sizes.size(); ++i) {
            m_elements.push_back({i, sizes[i] * area / sum});
        }

        std::sort(m_elements.begin(), m_elements.end(), [](Element a, Element b) {
            return a.size > b.size;
        });
    }

    ~Squarify() {}

    TreeMapNode computeWithHierarchy()
    {
        m_rectBuf.reserve(m_elements.size());
        return squarify(Span{0, m_elements.size()}, m_rect);
    }

    std::vector<Rect> compute()
    {
        const TreeMapNode tree = computeWithHierarchy();

        std::vector<TreeMapNode::Element> elements;
        gather(elements, tree);

        const auto compareIndex = [](const TreeMapNode::Element &a, const TreeMapNode::Element &b) {
            return a.index < b.index;
        };
        std::sort(elements.begin(), elements.end(), compareIndex);

        std::vector<Rect> ret;
        for (const TreeMapNode::Element &e : elements)
            ret.push_back(e.rect);

        return ret;
    }
};

} // namespace Squarify

#endif // _SQUARIFY_HPP_
