// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2020 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/bitcoinunits.h>
#include <chainparams.h>
#include <primitives/transaction.h>
#include <validation.h>

#include <QSettings>
#include <QStringList>

BitcoinUnits::BitcoinUnits(QObject *parent) :
        QAbstractListModel(parent),
        unitlist(availableUnits()) {
}

QList <BitcoinUnits::Unit> BitcoinUnits::availableUnits() {
    QList <BitcoinUnits::Unit> unitlist;
    unitlist.append(RTM);
    unitlist.append(mRTM);
    unitlist.append(uRTM);
    unitlist.append(ruffs);
    return unitlist;
}

bool BitcoinUnits::valid(int unit) {
    switch (unit) {
        case RTM:
        case mRTM:
        case uRTM:
        case ruffs:
            return true;
        default:
            return false;
    }
}

QString BitcoinUnits::name(int unit) {
    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        switch (unit) {
            case RTM:
                return QString("RTM");
            case mRTM:
                return QString("mRTM");
            case uRTM:
                return QString::fromUtf8("μRTM");
            case ruffs:
                return QString("ruffs");
            default:
                return QString("???");
        }
    } else {
        switch (unit) {
            case RTM:
                return QString("tRTM");
            case mRTM:
                return QString("mtRTM");
            case uRTM:
                return QString::fromUtf8("μtRTM");
            case ruffs:
                return QString("truffs");
            default:
                return QString("???");
        }
    }
}

QString BitcoinUnits::description(int unit) {
    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        switch (unit) {
            case RTM:
                return QString("Raptoreum");
            case mRTM:
                return QString("Milli-Raptoreum (1 / 1"
                THIN_SP_UTF8
                "000)");
            case uRTM:
                return QString("Micro-Raptoreum (1 / 1"
                THIN_SP_UTF8
                "000"
                THIN_SP_UTF8
                "000)");
            case ruffs:
                return QString("Ten Nano-Raptoreum (1 / 100"
                THIN_SP_UTF8
                "000"
                THIN_SP_UTF8
                "000)");
            default:
                return QString("???");
        }
    } else {
        switch (unit) {
            case RTM:
                return QString("TestRaptoreums");
            case mRTM:
                return QString("Milli-TestRaptoreum (1 / 1"
                THIN_SP_UTF8
                "000)");
            case uRTM:
                return QString("Micro-TestRaptoreum (1 / 1"
                THIN_SP_UTF8
                "000"
                THIN_SP_UTF8
                "000)");
            case ruffs:
                return QString("Ten Nano-TestRaptoreum (1 / 100"
                THIN_SP_UTF8
                "000"
                THIN_SP_UTF8
                "000)");
            default:
                return QString("???");
        }
    }
}

qint64 BitcoinUnits::factor(int unit) {
    switch (unit) {
        case RTM:
            return 100000000;
        case mRTM:
            return 100000;
        case uRTM:
            return 100;
        case ruffs:
            return 1;
        default:
            return 100000000;
    }
}

int BitcoinUnits::decimals(int unit) {
    switch (unit) {
        case RTM:
            return 8;
        case mRTM:
            return 5;
        case uRTM:
            return 2;
        case ruffs:
            return 0;
        default:
            return 0;
    }
}

qint64 BitcoinUnits::factorAsset(int unit) {
    switch (unit) {
        case 0:
            return 1;
        case 1:
            return 10;
        case 2:
            return 100;
        case 3:
            return 1000;
        case 4:
            return 10000;
        case 5:
            return 100000;
        case 6:
            return 1000000;
        case 7:
            return 10000000;
        case 8:
            return 100000000;
        default:
            return 100000000;
    }
}

QString
BitcoinUnits::format(int unit, const CAmount &nIn, bool fPlus, SeparatorStyle separators, const int nAssetUnit) {
    // Note: not using straight sprintf here because we do NOT want
    // localized number formatting.
    if ((nAssetUnit < 0 || nAssetUnit > 8) && !valid(unit))
        return QString(); // Refuse to format invalid unit
    qint64 n = (qint64) nIn;
    qint64 coin = nAssetUnit >= 0 ? factorAsset(nAssetUnit) : factor(unit);
    int num_decimals = nAssetUnit >= 0 ? nAssetUnit : decimals(unit);
    qint64 n_abs = (n > 0 ? n : -n);
    qint64 quotient = n_abs / coin;
    qint64 remainder = n_abs % coin;
    QString quotient_str = QString::number(quotient);
    QString remainder_str = QString::number(remainder).rightJustified(num_decimals, '0');

    // Use SI-style thin space separators as these are locale independent and can't be
    // confused with the decimal marker.
    QChar thin_sp(THIN_SP_CP);
    int q_size = quotient_str.size();
    if (separators == separatorAlways || (separators == separatorStandard && q_size > 4))
        for (int i = 3; i < q_size; i += 3)
            quotient_str.insert(q_size - i, thin_sp);

    if (n < 0)
        quotient_str.insert(0, '-');
    else if (fPlus && n > 0)
        quotient_str.insert(0, '+');

    if (num_decimals <= 0)
        return quotient_str;

    return quotient_str + QString(".") + remainder_str;
}


// NOTE: Using formatWithUnit in an HTML context risks wrapping
// quantities at the thousands separator. More subtly, it also results
// in a standard space rather than a thin space, due to a bug in Qt's
// XML whitespace canonicalisation
//
// Please take care to use formatHtmlWithUnit instead, when
// appropriate.

QString BitcoinUnits::formatWithUnit(int unit, const CAmount &amount, bool plussign, SeparatorStyle separators) {
    return format(unit, amount, plussign, separators) + QString(" ") + name(unit);
}

QString BitcoinUnits::formatWithCustomName(QString customName, const CAmount &amount, int unit, bool plussign,
                                           SeparatorStyle separators) {
    return format(RTM, amount / factorAsset(MAX_ASSET_UNITS - unit), plussign, separators, unit) + QString(" ") +
           customName.left(20);
}

QString BitcoinUnits::formatHtmlWithUnit(int unit, const CAmount &amount, bool plussign, SeparatorStyle separators) {
    QString str(formatWithUnit(unit, amount, plussign, separators));
    str.replace(QChar(THIN_SP_CP), QString(THIN_SP_HTML));
    return QString("<span style='white-space: nowrap;'>%1</span>").arg(str);
}

QString BitcoinUnits::formatHtmlWithCustomName(QString customName, QString uniqueId, int unit, const CAmount &amount,
                                               bool plussign, SeparatorStyle separators) {
    QString str(formatWithCustomName(customName, amount, unit, plussign, separators) + uniqueId);
    str.replace(QChar(THIN_SP_CP), QString(THIN_SP_HTML));
    return QString("<span style='white-space: nowrap;'>%1</span>").arg(str);
}

QString BitcoinUnits::floorWithUnit(int unit, const CAmount &amount, bool plussign, SeparatorStyle separators) {
    QSettings settings;
    int digits = settings.value("digits").toInt();

    QString result = format(unit, amount, plussign, separators);
    if (decimals(unit) > digits) result.chop(decimals(unit) - digits);

    return result + QString(" ") + name(unit);
}

QString BitcoinUnits::floorHtmlWithUnit(int unit, const CAmount &amount, bool plussign, SeparatorStyle separators) {
    QString str(floorWithUnit(unit, amount, plussign, separators));
    str.replace(QChar(THIN_SP_CP), QString(THIN_SP_HTML));
    return QString("<span style='white-space: nowrap;'>%1</span>").arg(str);
}

static bool parseAll(int num_decimals, const QString &value, CAmount *val_out, int maxSize) {
    // Ignore spaces and thin spaces when parsing
    QStringList parts = BitcoinUnits::removeSpaces(value).split(".");

    if (parts.size() > 2) {
        return false; // More than one dot
    }
    QString whole = parts[0];
    QString decimals;

    if (parts.size() > 1) {
        decimals = parts[1];
    }
    if (decimals.size() > num_decimals) {
        return false; // Exceeds max precision
    }
    bool ok = false;
    QString str = whole + decimals.leftJustified(num_decimals, '0');

    if (str.size() > maxSize) {
        return false; // Longer numbers will exceed 63 bits
    }
    CAmount retvalue(str.toLongLong(&ok));
    if (val_out) {
        *val_out = retvalue;
    }
    return ok;
}

bool BitcoinUnits::parse(int unit, const QString &value, CAmount *val_out) {
    if (!valid(unit) || value.isEmpty())
        return false; // Refuse to parse invalid unit or empty string
    int num_decimals = decimals(unit);

    return parseAll(num_decimals, value, val_out, 18);
}

bool BitcoinUnits::assetParse(int assetUnit, const QString &value, CAmount *val_out) {
    if (!(assetUnit >= 0 && assetUnit <= 8) || value.isEmpty())
        return false; // Refuse to parse invalid unit or empty string
    int num_decimals = assetUnit;

    return parseAll(num_decimals, value, val_out, MAX_ASSET_UNITS - num_decimals);
}

QString BitcoinUnits::getAmountColumnTitle(int unit) {
    QString amountTitle = QObject::tr("Amount");
    if (BitcoinUnits::valid(unit)) {
        amountTitle += " (" + BitcoinUnits::name(unit) + ")";
    }
    return amountTitle;
}

int BitcoinUnits::rowCount(const QModelIndex &parent) const {
    Q_UNUSED(parent);
    return unitlist.size();
}

QVariant BitcoinUnits::data(const QModelIndex &index, int role) const {
    return data(index.row(), role);
}

QVariant BitcoinUnits::data(const int &row, int role) const {
    if (row >= 0 && row < unitlist.size()) {
        Unit unit = unitlist.at(row);
        switch (role) {
            case Qt::EditRole:
            case Qt::DisplayRole:
                return QVariant(name(unit));
            case Qt::ToolTipRole:
                return QVariant(description(unit));
            case UnitRole:
                return QVariant(static_cast<int>(unit));
        }
    }
    return QVariant();
}

CAmount BitcoinUnits::maxMoney() {
    if (Params().IsFutureActive(::ChainActive().Tip())) {
        return MAX_MONEY;
    } else {
        return OLD_MAX_MONEY;
    }
}
