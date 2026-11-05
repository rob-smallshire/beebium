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

bool is_digit(QChar c) {
    return c >= QLatin1Char('0') && c <= QLatin1Char('9');
}

}  // namespace

int natural_compare(const QString& a, const QString& b) {
    qsizetype i = 0;
    qsizetype j = 0;
    while (i < a.size() && j < b.size()) {
        if (is_digit(a[i]) && is_digit(b[j])) {
            const qsizetype a_start = i;
            const qsizetype b_start = j;
            while (i < a.size() && is_digit(a[i])) {
                ++i;
            }
            while (j < b.size() && is_digit(b[j])) {
                ++j;
            }
            // By value: without leading zeros, the longer run is larger,
            // and runs of a length compare digit by digit.
            qsizetype a_first = a_start;
            while (a_first < i - 1 && a[a_first] == QLatin1Char('0')) {
                ++a_first;
            }
            qsizetype b_first = b_start;
            while (b_first < j - 1 && b[b_first] == QLatin1Char('0')) {
                ++b_first;
            }
            const qsizetype a_digits = i - a_first;
            const qsizetype b_digits = j - b_first;
            if (a_digits != b_digits) {
                return a_digits < b_digits ? -1 : 1;
            }
            for (qsizetype k = 0; k < a_digits; ++k) {
                if (a[a_first + k] != b[b_first + k]) {
                    return a[a_first + k] < b[b_first + k] ? -1 : 1;
                }
            }
            // Equal values: fewer leading zeros first.
            if (i - a_start != j - b_start) {
                return i - a_start < j - b_start ? -1 : 1;
            }
            continue;
        }
        const char32_t ca = a[i].toCaseFolded().unicode();
        const char32_t cb = b[j].toCaseFolded().unicode();
        if (ca != cb) {
            return ca < cb ? -1 : 1;
        }
        ++i;
        ++j;
    }
    // One is a prefix of the other: the shorter first.
    const qsizetype a_rest = a.size() - i;
    const qsizetype b_rest = b.size() - j;
    return a_rest == b_rest ? 0 : (a_rest < b_rest ? -1 : 1);
}

std::vector<PresetListing> library_order(std::vector<PresetListing> presets) {
    std::stable_sort(presets.begin(), presets.end(), [&](const PresetListing& a, const PresetListing& b) {
        const QString da = sortable_date(a.release_date);
        const QString db = sortable_date(b.release_date);
        if (da != db) {
            return da < db;
        }
        return natural_compare(a.name, b.name) < 0;
    });
    return presets;
}

QStringList duplicate_preset_arguments(const PresetListing& source, const QString& name) {
    return {QStringLiteral("create-preset"), QStringLiteral("--name"), name, QStringLiteral("--from"),
            source.id};
}

QStringList export_preset_arguments(const PresetListing& preset, const QString& filepath) {
    return {QStringLiteral("export-preset"), preset.id, QStringLiteral("--output"), filepath};
}

QStringList delete_preset_arguments(const PresetListing& preset) {
    return {QStringLiteral("delete-preset"), preset.id};
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
