#include "core/normalize.h"

#include "core/logging.h"

#include <QChar>

#include <string>
#include <unicode/uchar.h>
#include <unicode/unorm2.h>
#include <unicode/uscript.h>
#include <unicode/ustring.h>

namespace omnidict::core {

namespace {

using Text = std::u16string;

/// The code point starting at `i`, advancing `i` past it. An unpaired
/// surrogate comes back as itself, as ICU treats it.
UChar32 nextCodePoint(const Text& text, std::size_t& i)
{
    const char16_t lead = text[i++];
    if (QChar::isHighSurrogate(lead) && i < text.size() && QChar::isLowSurrogate(text[i])) {
        return static_cast<UChar32>(QChar::surrogateToUcs4(lead, text[i++]));
    }
    return lead;
}

/// Runs an ICU "fill this buffer" call, growing the buffer once when ICU
/// reports the size it needs. `call(dest, capacity, status)` returns the length.
template <typename Call>
Text fill(const Text& input, const char* what, Call call)
{
    Text out(input.size() + 16, u'\0');
    UErrorCode status = U_ZERO_ERROR;
    int32_t length = call(out.data(), static_cast<int32_t>(out.size()), &status);
    if (status == U_BUFFER_OVERFLOW_ERROR) {
        out.resize(static_cast<std::size_t>(length));
        status = U_ZERO_ERROR;
        length = call(out.data(), static_cast<int32_t>(out.size()), &status);
    }
    if (U_FAILURE(status) != 0) {
        qCWarning(lcCore) << "normalization step" << what << "failed:" << u_errorName(status);
        return input;
    }
    out.resize(static_cast<std::size_t>(length));
    return out;
}

using NormalizerGetter = const UNormalizer2* (*)(UErrorCode*);

Text normalized(const Text& input, NormalizerGetter getter, const char* what)
{
    UErrorCode status = U_ZERO_ERROR;
    const UNormalizer2* normalizer = getter(&status);
    if (U_FAILURE(status) != 0) {
        qCWarning(lcCore) << "no normalizer for" << what << ":" << u_errorName(status);
        return input;
    }
    return fill(input, what, [&](char16_t* dest, int32_t capacity, UErrorCode* error) {
        return unorm2_normalize(normalizer, input.data(), static_cast<int32_t>(input.size()), dest, capacity,
                                error);
    });
}

Text caseFolded(const Text& input)
{
    return fill(input, "case fold", [&](char16_t* dest, int32_t capacity, UErrorCode* error) {
        return u_strFoldCase(dest, capacity, input.data(), static_cast<int32_t>(input.size()),
                             U_FOLD_CASE_DEFAULT, error);
    });
}

/// Scripts where a base letter without its diacritics is the customary
/// search form. Everywhere else the marks are part of the spelling.
bool stripsMarks(UChar32 base)
{
    UErrorCode status = U_ZERO_ERROR;
    const UScriptCode script = uscript_getScript(base, &status);
    if (U_FAILURE(status) != 0) {
        return false;
    }
    return script == USCRIPT_LATIN || script == USCRIPT_CYRILLIC || script == USCRIPT_GREEK;
}

/// Expects decomposed (NFD) input.
Text withoutStrippableMarks(const Text& input)
{
    Text out;
    out.reserve(input.size());
    bool strip = false;
    std::size_t i = 0;
    while (i < input.size()) {
        const std::size_t start = i;
        const UChar32 codePoint = nextCodePoint(input, i);
        const int8_t category = u_charType(codePoint);
        const bool isMark = category == U_NON_SPACING_MARK || category == U_ENCLOSING_MARK ||
                            category == U_COMBINING_SPACING_MARK;
        if (!isMark) {
            strip = stripsMarks(codePoint);
        } else if (strip && category == U_NON_SPACING_MARK) {
            continue;
        }
        out.append(input, start, i - start);
    }
    return out;
}

/// Every run of White_Space becomes one space; none at either end.
Text withCollapsedWhitespace(const Text& input)
{
    Text out;
    out.reserve(input.size());
    bool pendingSpace = false;
    std::size_t i = 0;
    while (i < input.size()) {
        const std::size_t start = i;
        const UChar32 codePoint = nextCodePoint(input, i);
        if (u_isUWhiteSpace(codePoint) != 0) {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) {
            out.push_back(u' ');
            pendingSpace = false;
        }
        out.append(input, start, i - start);
    }
    return out;
}

} // namespace

QString normalizeHeadword(const QString& text)
{
    Text value = text.toStdU16String();
    value = normalized(value, unorm2_getNFKCInstance, "NFKC");
    value = caseFolded(value);
    value = normalized(value, unorm2_getNFDInstance, "NFD");
    value = withoutStrippableMarks(value);
    value = normalized(value, unorm2_getNFCInstance, "NFC");
    value = withCollapsedWhitespace(value);
    return QString::fromStdU16String(value);
}

} // namespace omnidict::core
