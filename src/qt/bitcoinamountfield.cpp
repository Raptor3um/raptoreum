// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/bitcoinamountfield.h>

#include <qt/bitcoinunits.h>
#include <qt/guiutil.h>

#include <QApplication>
#include <QAbstractSpinBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <iostream>//remove later

/**
 * Parse a string into a number of base monetary units and
 * return validity.
 * @note Must return 0 if !valid.
 */
static CAmount parse(const QString &text, int nUnit, int nAssetUnit, bool *valid_out = 0) {
    CAmount val = 0;
    bool valid = false;

    if (nAssetUnit >= 0)
        valid = BitcoinUnits::assetParse(nAssetUnit, text, &val);
    else
        valid = BitcoinUnits::parse(nUnit, text, &val);
    if (valid) {
        if (val < 0 || val > BitcoinUnits::maxMoney())
            valid = false;
    }
    if (valid_out)
        *valid_out = valid;
    return valid ? val : 0;
}

/** Amount widget validator, checks for valid CAmount value.
 */
class AmountValidator : public QValidator {
    Q_OBJECT
    int currentUnit;
    int assetUnit;
public:
    explicit AmountValidator(QObject *parent) :
            QValidator(parent),
            currentUnit(BitcoinUnits::RTM),
            assetUnit(-1) {}

    State validate(QString &input, int &pos) const override {
        if (input.isEmpty())
            return QValidator::Intermediate;
        bool valid = false;
        parse(input, currentUnit, assetUnit, &valid);
        /* Make sure we return Intermediate so that fixup() is called on defocus */
        return valid ? QValidator::Intermediate : QValidator::Invalid;
    }

    void updateUnit(int nUnit) {
        currentUnit = nUnit;
    }

    void updateAssetUnit(int nUnit) {
        assetUnit = nUnit;
    }
};

/** QLineEdit that uses fixed-point numbers internally and uses our own
 * formatting/parsing functions.
 */
class AmountLineEdit : public QLineEdit {
    Q_OBJECT
            AmountValidator
    *
    amountValidator;
public:
    explicit AmountLineEdit(QWidget *parent) :
            QLineEdit(parent),
            currentUnit(BitcoinUnits::RTM),
            assetUnit(-1) {
        setAlignment(Qt::AlignLeft);
        amountValidator = new AmountValidator(this);
        setValidator(amountValidator);
        connect(this, &QLineEdit::textEdited, this, &AmountLineEdit::valueChanged);
    }

    void fixup(const QString &input) {
        bool valid = false;
        CAmount val = parse(input, currentUnit, assetUnit, &valid);
        if (valid) {
            setText(BitcoinUnits::format(currentUnit, val, false, BitcoinUnits::separatorAlways, assetUnit));
        }
    }

    CAmount value(bool *valid_out = nullptr) const {
        return parse(text(), currentUnit, assetUnit, valid_out);
    }

    void setValue(const CAmount &value) {
        if (assetUnit >= 0)
            setText(BitcoinUnits::format(currentUnit, value / BitcoinUnits::factorAsset(MAX_ASSET_UNITS -  assetUnit) , false, BitcoinUnits::separatorAlways, assetUnit));
        else
            setText(BitcoinUnits::format(currentUnit, value, false, BitcoinUnits::separatorAlways, assetUnit));
        Q_EMIT valueChanged();
    }

    void setDisplayUnit(int unit) {
        bool valid = false;
        CAmount val = value(&valid);

        currentUnit = unit;
        amountValidator->updateUnit(unit);

        if (valid)
            setValue(val);
        else
            clear();
    }

    void setAssetUnit(int unit) {
        if (unit > MAX_ASSET_UNITS)
            unit = MAX_ASSET_UNITS;

        assetUnit = unit;
        amountValidator->updateAssetUnit(unit);

        bool valid = false;
        CAmount val = value(&valid);

        if (valid)
            setValue(val);
        else
            clear();
    }

    QSize minimumSizeHint() const override {
        ensurePolished();
        const QFontMetrics fm(fontMetrics());
        int h = 0;
        int w = GUIUtil::TextWidth(fm, BitcoinUnits::format(BitcoinUnits::RTM, BitcoinUnits::maxMoney(), false,
                                                            BitcoinUnits::separatorAlways));
        w += 2; // cursor blinking space
        w += GUIUtil::raptoreumThemeActive() ? 24 : 0; // counteract padding from css
        return QSize(w, h);
    }

private:
    int currentUnit;
    int assetUnit;

protected:
    bool event(QEvent *event) override {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
            QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() == Qt::Key_Comma) {
                // Translate a comma into a period
                QKeyEvent periodKeyEvent(event->type(), Qt::Key_Period, keyEvent->modifiers(), ".",
                                         keyEvent->isAutoRepeat(), keyEvent->count());
                return QLineEdit::event(&periodKeyEvent);
            }
            if (keyEvent->key() == Qt::Key_Enter || keyEvent->key() == Qt::Key_Return) {
                clearFocus();
            }
        }
        if (event->type() == QEvent::FocusOut) {
            fixup(text());
        }
        return QLineEdit::event(event);
    }

    Q_SIGNALS:
            void valueChanged();
};

#include <qt/bitcoinamountfield.moc>

BitcoinAmountField::BitcoinAmountField(QWidget *parent) :
        QWidget(parent),
        amount(nullptr),
        assetUnit(-1) {
    amount = new AmountLineEdit(this);
    amount->setLocale(QLocale::c());
    amount->installEventFilter(this);
    amount->setMaximumWidth(300);

    units = new BitcoinUnits(this);

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setSpacing(0);
    layout->setMargin(0);
    layout->addWidget(amount);

    setLayout(layout);

    setFocusPolicy(Qt::TabFocus);
    setFocusProxy(amount);

    // If one if the widgets changes, the combined content changes as well
    connect(amount, &AmountLineEdit::valueChanged, this, &BitcoinAmountField::valueChanged);
}

void BitcoinAmountField::clear() {
    amount->clear();
}

void BitcoinAmountField::setEnabled(bool fEnabled) {
    amount->setEnabled(fEnabled);
}

bool BitcoinAmountField::validate() {
    bool valid = false;
    value(&valid);
    setValid(valid);
    return valid;
}

void BitcoinAmountField::setValid(bool valid) {
    if (valid)
        amount->setStyleSheet("");
    else
        amount->setStyleSheet(GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_INVALID));
}

bool BitcoinAmountField::eventFilter(QObject *object, QEvent *event) {
    if (event->type() == QEvent::FocusIn) {
        // Clear invalid flag on focus
        setValid(true);
    }
    return QWidget::eventFilter(object, event);
}

QWidget *BitcoinAmountField::setupTabChain(QWidget *prev) {
    QWidget::setTabOrder(prev, amount);
    return amount;
}

CAmount BitcoinAmountField::value(bool *valid_out) const {
    if (assetUnit >= 0)
        return amount->value(valid_out) * BitcoinUnits::factorAsset(8 - assetUnit);
    else
        return amount->value(valid_out);
}

void BitcoinAmountField::setValue(const CAmount &value) {
    amount->setValue(value);
}

void BitcoinAmountField::setReadOnly(bool fReadOnly) {
    amount->setReadOnly(fReadOnly);
}

void BitcoinAmountField::unitChanged(int idx) {
    // Use description tooltip for current unit for the combobox
    amount->setToolTip(units->data(idx, Qt::ToolTipRole).toString());

    // Determine new unit ID
    int newUnit = units->data(idx, BitcoinUnits::UnitRole).toInt();

    amount->setPlaceholderText(tr("Amount in %1").arg(units->data(idx, Qt::DisplayRole).toString()));

    amount->setDisplayUnit(newUnit);
}

void BitcoinAmountField::setDisplayUnit(int newUnit) {
    unitChanged(newUnit);
}

void BitcoinAmountField::setAssetsUnit(int unit) {
    assetUnit = unit;
    amount->setAssetUnit(assetUnit);
}
