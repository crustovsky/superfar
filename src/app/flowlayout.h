// A row of widgets that wraps: what does not fit the width goes on the
// next line, so a panel's row of controls never sets how wide the
// window must be (the least it asks for is its widest single item).
// Room left over on a line goes to the items that can expand, as a
// QHBoxLayout would give it. Items are widgets; put a label and its
// field in one cell() so they stay together.
#pragma once

#include <QLayout>
#include <QList>

class FlowLayout : public QLayout {
public:
    explicit FlowLayout(int spacing = 6);
    ~FlowLayout() override;

    // label and field as one item; it expands when the field does.
    static QWidget* cell(std::initializer_list<QWidget*> widgets, QWidget* parent);

    void addItem(QLayoutItem* item) override;
    int count() const override;
    QLayoutItem* itemAt(int index) const override;
    QLayoutItem* takeAt(int index) override;
    Qt::Orientations expandingDirections() const override { return Qt::Horizontal; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;
    QSize minimumSize() const override;
    void setGeometry(const QRect& rect) override;

private:
    int arrange(const QRect& rect, bool apply) const;

    QList<QLayoutItem*> items_;
    int spacing_;
};
