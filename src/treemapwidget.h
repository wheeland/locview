#pragma once

#include <QOpenGLWidget>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QTimer>

#include "squarify.h"
#include "treemaplayouter.h"

class TreeMapWidget : public QOpenGLWidget, public TreeMapLayouter
{
    Q_OBJECT
    Q_PROPERTY(int maxDepth READ maxDepth WRITE setMaxDepth)
    Q_PROPERTY(int maxSize READ maxSize WRITE setMaxSize)

public:
    TreeMapWidget(QWidget *parent = nullptr);
    ~TreeMapWidget();

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

    QRectF getTextBounds(const QString &text) const override;
    void onNodeTreeChanged() override;
    void onLayoutChanged() override;
    void onViewportChanged() override;

private slots:
    void onResize();

private:
    void setSelectedNode(const Node *node, QPoint mouse);
    void setHoveredNode(const Node *node, QPoint mouse);

    // Keep track of where we clicked when panning
    bool m_mouseDown = false;
    bool m_isPanning = false;
    QRectF m_mouseDownViewport;
    QPointF m_mouseDownViewPos;
    QPointF m_mouseDownModelPos;

    const Node *m_hoveredNode = nullptr;
    const Node *m_selectedNode = nullptr;

    // after widget resizing, we only actually re-compute the scene after
    // waiting for a short amount of time
    QTimer m_resizeTimer;
    QSize m_oldSize;

    QOpenGLShaderProgram m_shader;
    int m_shaderLocPos;
    int m_shaderLocRect;
    int m_shaderLocBgColor;
    int m_shaderLocFadeColor;

    QOpenGLBuffer m_quadVertexBuffer;

    QOpenGLBuffer m_nodeInstanceBuffer;
    bool m_nodeInstanceBufferDirty = true;
    int m_nodeInstancesCount = 0;

    QOpenGLBuffer m_groupInstanceBuffer;
};
