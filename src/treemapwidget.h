#pragma once

#include <QOpenGLWidget>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>

#include "squarify.h"

struct TreeMapNode
{
    QString label;
    QString groupLabel;
    QColor color;
    float size = 0.0f;
    QVector<TreeMapNode> children;
    void *userData = nullptr;
};

class TreeMapWidget : public QOpenGLWidget
{
    Q_OBJECT
    Q_PROPERTY(int maxDepth READ maxDepth WRITE setMaxDepth)
    Q_PROPERTY(int maxSize READ maxSize WRITE setMaxSize)

public:
    TreeMapWidget(QWidget *parent = nullptr);
    ~TreeMapWidget();

    void setRootNode(const TreeMapNode &root);

    int maxDepth() const { return m_maxDepth; }
    void setMaxDepth(int maxDepth);

    int maxSize() const { return m_maxSize; }
    void setMaxSize(int maxSize);

    int minGroupSize() const { return m_minGroupSize; }
    void setMinGroupSize(int minGroupSize);

public slots:
    void zoomIn(void *userData);
    void zoomOut();

signals:
    void nodeSelected(void *userData, QPoint mouse);
    void nodeHovered(void *userData, QPoint mouse);
    void nodeRightClicked(void *userData, QPoint mouse);

protected:
    void resizeEvent(QResizeEvent *event) override;

    void initializeGL() override;
    void paintGL() override;

    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    enum NodeRenderState
    {
        CulledViewport,
        CulledDepth,
        Render,
        RenderChildren
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
    Node m_root;
    Node *m_renderedNode = &m_root;

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
    void updateCulling(TreeNode &treeNode, bool fullyVisible = false);
    void updateGroupRendering(TreeNode *treeNode);

    void setViewport(QRectF rect);

    using NodeTraversalFunctor = std::function<bool(const Node&)>;
    void traverseRenderNodes(const Node &node, const NodeTraversalFunctor &visitor);

    /** Given the currently rendered tree, check which node is displayed at the given coords */
    const Node *getNodeAt(QPoint pt, const Node *parent) const;

    Node *getNodeWithUserData(Node *node, void *data) const;

    void setSelectedNode(const Node *node, QPoint mouse);
    void setHoveredNode(const Node *node, QPoint mouse);

    QRectF sceneToView(const QRectF &rect) const;
    QRectF viewToScene(const QRectF &rect) const;
    QPointF viewToScene(const QPointF &pt) const;

    QRectF m_viewport;

    // Keep track of where we clicked when panning
    bool m_mouseDown = false;
    bool m_isPanning = false;
    QRectF m_mouseDownViewport;
    QPointF m_mouseDownViewPos;
    QPointF m_mouseDownModelPos;

    const Node *m_hoveredNode = nullptr;
    const Node *m_selectedNode = nullptr;

    QVector<Node*> m_zoomStack;

    int m_maxDepth = -1;
    int m_maxSize = 20;
    int m_minGroupSize = 50;

    QOpenGLShaderProgram m_shader;
    QOpenGLBuffer m_nodesBuffer;
    QOpenGLBuffer m_groupsBuffer;
    bool m_nodesBufferDirty = true;
    int m_nodesCount = 0;
};
