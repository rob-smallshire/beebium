// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. Beebium is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Beebium.
// If not, see <https://www.gnu.org/licenses/>.

#include "servers/PresetLibrary.hpp"

#include <QCollator>
#include <QRegularExpression>

#include <algorithm>

namespace beebium::linux_client {

namespace {

// "1981", "1981-12", "1981-12-01" as YYYY-MM-DD; undated after any date.
QString sortable_date(const QString& date) {
    const QStringList parts = date.trimmed().split(QLatin1Char('-'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return QStringLiteral("9999-99-99");
    }
    const QString year = parts.value(0).rightJustified(4, QLatin1Char('0'));
    const QString month = parts.value(1, QStringLiteral("00")).rightJustified(2, QLatin1Char('0'));
    const QString day = parts.value(2, QStringLiteral("00")).rightJustified(2, QLatin1Char('0'));
    return year + QLatin1Char('-') + month + QLatin1Char('-') + day;
}

}  // namespace

std::vector<PresetListing> library_order(std::vector<PresetListing> presets) {
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::stable_sort(presets.begin(), presets.end(), [&](const PresetListing& a, const PresetListing& b) {
        const QString da = sortable_date(a.release_date);
        const QString db = sortable_date(b.release_date);
        if (da != db) {
            return da < db;
        }
        return collator.compare(a.name, b.name) < 0;
    });
    return presets;
}

QString duplicate_name(const QString& name, const QStringList& taken) {
    static const QRegularExpression COPY(QStringLiteral("^(.*) Copy(?: (\\d+))?$"));
    const auto match = COPY.match(name);
    const QString base = match.hasMatch() ? match.captured(1) : name;
    QString candidate = base + QStringLiteral(" Copy");
    for (int n = 2; taken.contains(candidate); ++n) {
        candidate = base + QStringLiteral(" Copy %1").arg(n);
    }
    return candidate;
}

}  // namespace beebium::linux_client
