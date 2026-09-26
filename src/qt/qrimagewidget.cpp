// Copyright (c) 2011-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/qrimagewidget.h>

#include <qt/guiutil.h>

#include <QApplication>
#include <QClipboard>
#include <QDrag>
#include <QFontDatabase>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>

#include <bitcoin-build-config.h> // IWYU pragma: keep

#ifdef USE_QRCODE
#include <qrencode.h>
#endif

QRImageWidget::QRImageWidget(QWidget* parent)
    : QLabel(parent)
{
    contextMenu = new QMenu(this);
    contextMenu->addAction(tr("&Save Image…"), this, &QRImageWidget::saveImage);
    contextMenu->addAction(tr("&Copy Image"), this, &QRImageWidget::copyImage);
}

bool QRImageWidget::setQR(const QString& data, const QString& text)
{
    QFont font{GUIUtil::fixedPitchFont()};
    font.setStretch(QFont::SemiCondensed);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 1);
    if (!text.isEmpty()) {
        font.setPointSizeF(GUIUtil::calculateIdealFontSize(QR_IMAGE_SIZE, text, font));
    }
    return setQR(data, text, font);
}

bool QRImageWidget::setQR(const QString& data, const QString& text, const QFont& font)
{
#ifdef USE_QRCODE
    setText("");
    if (data.isEmpty()) return false;

    // limit length
    if (data.length() > MAX_URI_LENGTH) {
        setText(tr("Resulting URI too long, try to reduce the text for label / message."));
        return false;
    }

    QRcode *code = QRcode_encodeString(data.toUtf8().constData(), 0, QR_ECLEVEL_L, QR_MODE_8, 1);

    if (!code) {
        setText(tr("Error encoding URI into QR Code."));
        return false;
    }

    // Preserve the four-module quiet zone required by QR readers before scaling.
    QImage qrImage{code->width + 8, code->width + 8, QImage::Format_RGB32};
    qrImage.fill(0xffffff);
    unsigned char *p = code->data;
    for (int y = 0; y < code->width; ++y) {
        for (int x = 0; x < code->width; ++x) {
            qrImage.setPixel(x + 4, y + 4, ((*p & 1) ? 0x0 : 0xffffff));
            ++p;
        }
    }
    QRcode_free(code);

    int qr_image_width{QR_IMAGE_SIZE + (2 * QR_IMAGE_MARGIN)};
    int qr_image_height{qr_image_width};
    int qr_image_x_margin{QR_IMAGE_MARGIN};
    int text_lines{0};
    if (!text.isEmpty()) {
        const int max_text_width{qr_image_width - (2 * QR_IMAGE_TEXT_MARGIN)};
        const QFontMetrics fm{font};
        const int text_width{GUIUtil::TextWidth(fm, text)};
        if (text_width > max_text_width && text_width < max_text_width * 5 / 4) {
            qr_image_width = text_width + (2 * QR_IMAGE_TEXT_MARGIN);
            qr_image_x_margin = (qr_image_width - QR_IMAGE_SIZE) / 2;
            text_lines = 1;
        } else {
            text_lines = (text_width + max_text_width - 1) / max_text_width;
        }
        qr_image_height += (fm.height() * text_lines) + QR_IMAGE_TEXT_MARGIN;
    }
    QImage qrAddrImage{qr_image_width, qr_image_height, QImage::Format_RGB32};
    qrAddrImage.fill(0xffffff);
    {
        QPainter painter(&qrAddrImage);
        painter.drawImage(qr_image_x_margin, QR_IMAGE_MARGIN, qrImage.scaled(QR_IMAGE_SIZE, QR_IMAGE_SIZE));

        if (!text.isEmpty()) {
            QRect padded_rect{qrAddrImage.rect()};
            padded_rect.setHeight(padded_rect.height() - QR_IMAGE_TEXT_MARGIN);
            QString text_wrapped{text};
            const int chars_per_line{(text.size() + text_lines - 1) / text_lines};
            for (int line{1}, pos{0}; line < text_lines; ++line) {
                pos += chars_per_line;
                text_wrapped.insert(pos, QChar{'\n'});
            }
            painter.setFont(font);
            painter.drawText(padded_rect, Qt::AlignBottom | Qt::AlignCenter, text_wrapped);
        }
    }

    setPixmap(QPixmap::fromImage(qrAddrImage));

    return true;
#else
    setText(tr("QR code support not available."));
    return false;
#endif
}

QImage QRImageWidget::exportImage()
{
    return GUIUtil::GetImage(this);
}

void QRImageWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && GUIUtil::HasPixmap(this)) {
        event->accept();
        QMimeData *mimeData = new QMimeData;
        mimeData->setImageData(exportImage());

        QDrag *drag = new QDrag(this);
        drag->setMimeData(mimeData);
        drag->exec();
    } else {
        QLabel::mousePressEvent(event);
    }
}

void QRImageWidget::saveImage()
{
    if (!GUIUtil::HasPixmap(this))
        return;
    QString fn = GUIUtil::getSaveFileName(
        this, tr("Save QR Code"), QString(),
        /*: Expanded name of the PNG file format.
            See: https://en.wikipedia.org/wiki/Portable_Network_Graphics. */
        tr("PNG Image") + QLatin1String(" (*.png)"), nullptr);
    if (!fn.isEmpty())
    {
        exportImage().save(fn);
    }
}

void QRImageWidget::copyImage()
{
    if (!GUIUtil::HasPixmap(this))
        return;
    QApplication::clipboard()->setImage(exportImage());
}

void QRImageWidget::contextMenuEvent(QContextMenuEvent *event)
{
    if (!GUIUtil::HasPixmap(this))
        return;
    contextMenu->exec(event->globalPos());
}
