#pragma once

#include "squarify.h"
#include <QString>
#include <QColor>
#include <QRectF>

struct TreeMapNode
{
    QString label;
    QString groupLabel;
    QColor color;
    float size = 0.0f;
    QVector<TreeMapNode> children;
    void *userData = nullptr;
};

class TreeMapLayouter
{
public:
    void setRootNode(const TreeMapNode &root);

    int maxDepth() const { return m_maxDepth; }
    void setMaxDepth(int maxDepth);

    int maxSize() const { return m_maxSize; }
    void setMaxSize(int maxSize);

    int minGroupSize() const { return m_minGroupSize; }
    void setMinGroupSize(int minGroupSize);

    void zoomIn(void *userData);
    void zoomOut();

protected:
    TreeMapLayouter(int width, int height);
    ~TreeMapLayouter();

    virtual QRectF getTextBounds(const QString &text) const = 0;
    virtual void onNodeTreeChanged() = 0;
    virtual void onLayoutChanged() = 0;
    virtual void onViewportChanged() = 0;

    void resize(int width, int height);
    void setViewport(const QRectF &rect);
    QRectF sceneToView(const QRectF &rect) const;
    QRectF viewToScene(const QRectF &rect) const;
    QPointF viewToScene(const QPointF &pt) const;

    enum NodeRenderState
    {
        CulledViewport, // not visible because not in viewport
        CulledDepth,    // not visible because too deep
        CulledChildren, // not visible because the parent is rendered
        Render,         // rendered
        RenderChildren  // not rendered, children are rendered instead
    };

    struct Node
    {
        // data set by user
        QString label;
        QString groupLabel;
        QRectF groupLabelBounds;
        QColor color;
        float size = 0.0f;
        void *userData = nullptr;
        int depth = 0;
        QVector<Node> children;

        // data updated on recalculate
        int treeDepth = 0;
        QRectF sceneRect;

        // data updated on viewport change
        QRectF viewRect;
        NodeRenderState renderState = CulledViewport;

        bool responsibleForGroup = true;
        QRectF groupViewRect;
    };
    Node *m_renderedNode = &m_root;

    using NodeTraversalFunctor = std::function<bool(const Node&)>;
    void traverseRenderNodes(const Node &node, const NodeTraversalFunctor &visitor);

    /** Given the currently rendered tree, check which node is displayed at the given coords */
    const Node *getNodeAt(QPoint pt, const Node *parent) const;

    Node *getNodeWithUserData(Node *node, void *data) const;

    QRectF m_viewport;

private:
    int m_width;
    int m_height;

    int m_maxDepth = -1;
    int m_maxSize = 20;
    int m_minGroupSize = 50;

    Node m_root;
    QVector<Node*> m_zoomStack;

    /** Corresponds to a squarified data node */
    struct TreeNode
    {
        struct Subdivision
        {
            /** rect of this subdivision and all that follow within this node */
            QRectF remainingSceneRect;
            QRectF remainingViewRect;
            QVector<TreeNode> subnodes;
        };

        Node *node = nullptr;
        QVector<Subdivision> subdivisions;
    };
    TreeNode m_treeRoot;

    void rebuildNodeTree(Node &dstNode, const TreeMapNode &srcNode, int depth);
    static void relayoutTreeMapping(TreeNode &treeNode, Node &node, const QRectF &rect);
    void updateCulling(TreeNode &treeNode, bool fullyVisible = false, bool culledParent = false);
    void updateGroupRendering(TreeNode *treeNode);
};
