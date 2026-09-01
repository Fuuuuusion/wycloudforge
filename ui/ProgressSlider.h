#pragma once

#include <QWidget>

class ProgressSlider : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int value READ value WRITE setValue NOTIFY valueChanged)
public:
    explicit ProgressSlider(QWidget *parent = nullptr);

    void setRange(int min, int max);
    int minimum() const { return m_minimum; }
    int maximum() const { return m_maximum; }
    int value() const { return m_value; }
    bool isDragging() const { return m_dragging; }
    void setValue(int value, bool emitSignal = true);

    void setShowHandle(bool show) { m_showHandle = show; update(); }

signals:
    void valueChanged(int value);
    void dragStarted();
    void seekFinished(int value);
    void dragFinished();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    int valueFromX(int x) const;

    int m_minimum = 0;
    int m_maximum = 100;
    int m_value = 0;
    bool m_hover = false;
    bool m_dragging = false;
    bool m_showHandle = true;
};
