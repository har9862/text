/*
 * Copyright © 2016 Andrew Penkrat
 *
 * This file is part of Liri Text.
 *
 * Liri Text is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Liri Text is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Liri Text.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "languageloader.h"
#include <QFile>
#include <QDebug>
#include <QRegularExpression>
#include "languagemanager.h"

LanguageLoader::LanguageLoader() { }

LanguageLoader::LanguageLoader(QSharedPointer<LanguageDefaultStyles> defaultStyles) {
    for (auto styleId : defaultStyles->styles.keys()) {
        m_themeStyles += styleId;
        m_styleMap[styleId] = styleId;
    }
}

LanguageLoader::~LanguageLoader() {
    for (auto contextRef : m_knownContexts) {
        if(!contextRef->context->inUse())
            contextRef->context->prepareForRemoval();
    }
    for (auto contextRef : m_originalContexts) {
        if(!contextRef->context->inUse())
            contextRef->context->prepareForRemoval();
    }
}

QSharedPointer<LanguageContextReference> LanguageLoader::loadMainContextById(QString id) {
    qDebug() << "Loading" << id;
    QString path = LanguageManager::pathForId(id);
    return loadMainContext(path);
}

QSharedPointer<LanguageContextReference> LanguageLoader::loadMainContextByMimeType(QMimeType mimeType, QString filename) {
    QString path = LanguageManager::pathForMimeType(mimeType, filename);
    return loadMainContext(path);
}

QSharedPointer<LanguageContextReference> LanguageLoader::loadMainContext(QString path) {
    QFile file(path);
    QString langId;
    if(file.open(QFile::ReadOnly)) {
        QXmlStreamReader xml(&file);
        while (!xml.atEnd()) {
            xml.readNext();
            if(xml.isStartElement()) {
                if(xml.name() == "language") {
                    langId = xml.attributes().value("id").toString();
                    m_languageDefaultOptions   [langId] = QRegularExpression::OptimizeOnFirstUsageOption;
                    m_languageLeftWordBoundary [langId] = "\\b";
                    m_languageRightWordBoundary[langId] = "\\b";
                }
                if(xml.name() == "styles")
                    parseStyles(xml, langId);
                if(xml.name() == "default-regex-options")
                    parseDefaultRegexOptions(xml, langId);
                if(xml.name() == "keyword-char-class")
                    parseWordCharClass(xml, langId);
                if(xml.name() == "definitions")
                    parseDefinitions(xml, langId);
            }
        }
    }
    file.close();
    QString contextId = langId + ":" + langId;
    if(m_knownContexts.keys().contains(contextId)) {
        auto mainContext = m_knownContexts[contextId];
        mainContext->context->markAsInUse();
        return mainContext;
    }
    else
        return QSharedPointer<LanguageContextReference>();
}

LanguageMetadata LanguageLoader::loadMetadata(QString path) {
    LanguageMetadata result;
    QFile file(path);
    if(file.open(QFile::ReadOnly)) {
        QXmlStreamReader xml(&file);
        while (!xml.atEnd()) {
            xml.readNext();
            if(xml.isStartElement()) {
                if(xml.name() == "language") {
                    result.id = xml.attributes().value("id").toString();
                    if(xml.attributes().hasAttribute("_name")) // Translatable
                        result.name = xml.attributes().value("_name").toString();
                    else
                        result.name = xml.attributes().value("name").toString();
                }
                if(xml.name() == "metadata") {
                    parseMetadata(xml, result);
                    break;
                }
            }
        }
    }
    file.close();
    return result;
}

void LanguageLoader::loadDefinitionsAndStylesById(QString id) {
    QString path = LanguageManager::pathForId(id);
    loadDefinitionsAndStyles(path);
}

void LanguageLoader::loadDefinitionsAndStyles(QString path) {
    QString langId;
    QFile file(path);
    if(file.open(QFile::ReadOnly)) {
        QXmlStreamReader xml(&file);
        while (!xml.atEnd()) {
            xml.readNext();
            if(xml.isStartElement()) {
                if(xml.name() == "language") {
                    langId = xml.attributes().value("id").toString();
                    m_languageDefaultOptions   [langId] = QRegularExpression::OptimizeOnFirstUsageOption;
                    m_languageLeftWordBoundary [langId] = "\\b";
                    m_languageRightWordBoundary[langId] = "\\b";
                }
                if(xml.name() == "styles")
                    parseStyles(xml, langId);
                if(xml.name() == "default-regex-options")
                    parseDefaultRegexOptions(xml, langId);
                if(xml.name() == "keyword-char-class")
                    parseWordCharClass(xml, langId);
                if(xml.name() == "definitions")
                    parseDefinitions(xml, langId);
            }
        }
    }
    file.close();
}

void LanguageLoader::parseMetadata(QXmlStreamReader &xml, LanguageMetadata &metadata) {
    while (!(xml.name() == "metadata" && xml.isEndElement())) {
        xml.readNext();
        if(xml.name() == "property") {
            QStringRef pName = xml.attributes().value("name");
            if(pName == "mimetypes")
                metadata.mimeTypes = xml.readElementText();
            if(pName == "globs")
                metadata.globs = xml.readElementText();
            // Note: metadata can also have line-comment and block-comment properties
        }
    }
}

void LanguageLoader::parseStyles(QXmlStreamReader &xml, QString langId) {
    while (!(xml.name() == "styles" && xml.isEndElement())) {
        xml.readNext();
        if(xml.name() == "style")
            parseStyle(xml, langId);
    }
}

void LanguageLoader::parseDefinitions(QXmlStreamReader &xml, QString langId) {
    while (!(xml.name() == "definitions" && xml.isEndElement())) {
        xml.readNext();
        if(xml.name() == "define-regex")
            parseDefineRegex(xml, langId);
        if(xml.name() == "context")
            parseContext(xml, langId);
        if(xml.name() == "replace")
            parseReplace(xml, langId);
    }
}

QSharedPointer<LanguageContextReference> LanguageLoader::parseContext(QXmlStreamReader &xml, QString langId, QXmlStreamAttributes additionalAttributes) {
    QSharedPointer<LanguageContextReference> result;
    QSharedPointer<LanguageContextReference> resultCopy; // Will be referenced by original references
    QXmlStreamAttributes contextAttributes = xml.attributes();
    contextAttributes += additionalAttributes;
    QString id = contextAttributes.value("id").toString();

    if(id != "" && m_knownContexts.keys().contains(langId + ":" + id))
        result = m_knownContexts[langId + ":" + id];
    else
        result = QSharedPointer<LanguageContextReference>(new LanguageContextReference());

    if(id != "" && m_originalContexts.keys().contains(langId + ":" + id))
        resultCopy = m_originalContexts[langId + ":" + id];
    else
        resultCopy = QSharedPointer<LanguageContextReference>(new LanguageContextReference());

    if(contextAttributes.hasAttribute("ref")) {
        QStringRef refId = contextAttributes.value("ref");
        if(refId.contains(':') && !m_knownContexts.keys().contains(refId.toString())) {
            loadDefinitionsAndStylesById(refId.left(refId.indexOf(':')).toString());
        }
        QString refIdCopy = refId.toString();
        if(!refIdCopy.contains(':'))
            refIdCopy = langId + ":" + refIdCopy;
        if(m_knownContexts.keys().contains(refIdCopy)) {
            if(contextAttributes.hasAttribute("original")) {
                result->context = m_originalContexts[refIdCopy]->context;
                result->styleId = m_originalContexts[refIdCopy]->styleId;
            } else {
                result->context = m_knownContexts[refIdCopy]->context;
                result->styleId = m_knownContexts[refIdCopy]->styleId;
            }
        } else {
            // Predefinition
            m_knownContexts[refIdCopy] = result;
            m_originalContexts[refIdCopy] = resultCopy;
        }
    }

    if(id != "") {
        m_knownContexts[langId + ":" + id] = result;
        m_originalContexts[langId + ":" + id] = resultCopy;
    }

    QString kwPrefix = "\\%[", kwSuffix = "\\%]";

    QString styleId = result->styleId;
    if(contextAttributes.hasAttribute("style-ref")) {
        QStringRef styleIdRef = xml.attributes().value("style-ref");
        if(styleIdRef.contains(':') && !m_styleMap.keys().contains(styleIdRef.toString()))
            loadDefinitionsAndStylesById(styleIdRef.left(styleIdRef.indexOf(':')).toString());
        styleId = styleIdRef.toString();
        if(!styleId.contains(':'))
            styleId = langId + ":" + styleId;
    }
    if(contextAttributes.hasAttribute("ignore-style"))
        styleId.clear();
    applyStyleToContext(result, styleId);

    if(contextAttributes.hasAttribute("sub-pattern")) {
        if(result->context->type != LanguageContext::SubPattern)
            result->context->init(LanguageContext::SubPattern, contextAttributes);
    }

    xml.readNext();
    while (!(xml.name() == "context" && xml.isEndElement())) {
        if(xml.name() == "start") {
            if(result->context->type != LanguageContext::Container)
                result->context->init(LanguageContext::Container, contextAttributes);

            auto options = parseRegexOptions(xml, langId);
            result->context->container.start = resolveRegex((options & QRegularExpression::ExtendedPatternSyntaxOption) != 0 ? xml.readElementText() :
                                                                                            escapeNonExtended( xml.readElementText() ),
                                             options | QRegularExpression::ExtendedPatternSyntaxOption, langId);
        }
        if(xml.name() == "end") {
            if(result->context->type != LanguageContext::Container)
                result->context->init(LanguageContext::Container, contextAttributes);

            auto options = parseRegexOptions(xml, langId);
            result->context->container.end = resolveRegex((options & QRegularExpression::ExtendedPatternSyntaxOption) != 0 ? xml.readElementText() :
                                                                                          escapeNonExtended( xml.readElementText() ),
                                           options | QRegularExpression::ExtendedPatternSyntaxOption, langId);
        }
        if(xml.name() == "match") {
            if(result->context->type != LanguageContext::Simple)
                result->context->init(LanguageContext::Simple, contextAttributes);

            auto options = parseRegexOptions(xml, langId);
            result->context->simple.match = resolveRegex((options & QRegularExpression::ExtendedPatternSyntaxOption) != 0 ? xml.readElementText() :
                                                                                         escapeNonExtended( xml.readElementText() ),
                                          options | QRegularExpression::ExtendedPatternSyntaxOption, langId);
        }
        if(xml.name() == "prefix") {
            /* According to https://developer.gnome.org/gtksourceview/stable/lang-reference.html
             * prefix is a regex in form of define-regex, which means it can have it's own regex options.
             * Howether, in practice none of prebundled languages have them.
             * Futhermore, making prefix an isolated group breaks highlighting for some languages.
             * Following these considerations, prefixes and suffixes are taken in their original form.
             */
            kwPrefix = xml.readElementText();
        }
        if(xml.name() == "suffix") {
            kwSuffix = xml.readElementText();
        }
        if(xml.name() == "keyword") {
            if(result->context->type != LanguageContext::Container)
                result->context->init(LanguageContext::Container, contextAttributes);

            auto inc = QSharedPointer<LanguageContextReference>(new LanguageContextReference);
            inc->context->init(LanguageContext::Keyword, contextAttributes);
            applyStyleToContext(inc, styleId);

            auto options = parseRegexOptions(xml, langId);
            inc->context->keyword.keyword = resolveRegex(kwPrefix + xml.readElementText() + kwSuffix, options, langId);
            result->context->container.includes.append(inc);
        }
        if(xml.name() == "include") {
            xml.readNext();
            while (!(xml.name() == "include" && xml.isEndElement())) {
                if(xml.name() == "context") {
                    if(result->context->type == LanguageContext::Undefined)
                        result->context->init(LanguageContext::Container, contextAttributes);

                    if(result->context->type == LanguageContext::Simple) {
                        auto inc = parseContext(xml, langId);
                        if(inc)
                            result->context->simple.includes.append(inc);
                    } else if(result->context->type == LanguageContext::Container) {
                        QXmlStreamAttributes childrenAttributes;
                        if(result->context->container.start.pattern() == "" && contextAttributes.hasAttribute("once-only"))
                            childrenAttributes += QXmlStreamAttribute("once-only", contextAttributes.value("once-only").toString());
                        auto inc = parseContext(xml, langId, childrenAttributes);

                        if(inc)
                            result->context->container.includes.append(inc);
                    } else {
                        Q_ASSERT(false);
                    }
                }
                xml.readNext();
            }
        }
        xml.readNext();
    }

    *resultCopy->context = *result->context;
    resultCopy->styleId = result->styleId;
    return result;
}

void LanguageLoader::parseStyle(QXmlStreamReader &xml, QString langId) {
    QString id = langId + ":" + xml.attributes().value("id").toString();

    QString mapId;
    if(!m_themeStyles.contains(id) && xml.attributes().hasAttribute("map-to")) {
        QString refId = xml.attributes().value("map-to").toString();
        if(refId.contains(':') && !m_styleMap.keys().contains(refId)) {
            loadDefinitionsAndStylesById(refId.left(refId.indexOf(':')));
        }
        if(!refId.contains(':'))
            refId = langId + ":" + refId;
        if(m_styleMap.keys().contains(refId))
            mapId = m_styleMap[refId];
        else
            mapId = refId;
    } else
        mapId = id;

    for (QString key : m_styleMap.keys())
        if(m_styleMap[key] == id)
            m_styleMap[key] = mapId;
    m_styleMap[id] = mapId;

    xml.skipCurrentElement();
}

QRegularExpression::PatternOptions LanguageLoader::parseRegexOptions(QXmlStreamReader &xml, QString langId) {
    auto result = m_languageDefaultOptions[langId];
    if(xml.attributes().hasAttribute("case-sensitive")) {
        bool caseInsensitive = xml.attributes().value("case-sensitive") == "false";
        if(caseInsensitive)
            result |= QRegularExpression::CaseInsensitiveOption;
        else
            result &= ~QRegularExpression::CaseInsensitiveOption;
    }
    if(xml.attributes().hasAttribute("extended")) {
        bool extended = xml.attributes().value("extended") == "true";
        if(extended)
            result |= QRegularExpression::ExtendedPatternSyntaxOption;
        else
            result &= ~QRegularExpression::ExtendedPatternSyntaxOption;
    }
    if(xml.attributes().hasAttribute("dupnames")) {
        // Not supported
    }
    return result;
}

void LanguageLoader::parseDefaultRegexOptions(QXmlStreamReader &xml, QString langId) {
    m_languageDefaultOptions[langId] = parseRegexOptions(xml, langId);
    xml.readNext();
}

void LanguageLoader::parseDefineRegex(QXmlStreamReader &xml, QString langId) {
    QString id = xml.attributes().value("id").toString();
    auto options = parseRegexOptions(xml, langId);
    m_knownRegexes[id] = applyOptionsToSubRegex(xml.readElementText(), options);
}

void LanguageLoader::parseWordCharClass(QXmlStreamReader &xml, QString langId) {
    QString charClass = xml.readElementText();
    m_languageLeftWordBoundary [langId] = QStringLiteral("(?<!%1)(?=%1)").arg(charClass);
    m_languageRightWordBoundary[langId] = QStringLiteral("(?<=%1)(?!%1)").arg(charClass);
}

void LanguageLoader::parseReplace(QXmlStreamReader &xml, QString langId) {
    QString id = xml.attributes().value("id").toString();
    QString refId = xml.attributes().value("ref").toString();
    if(!id.contains(':'))
        id.prepend(langId + ":");
    if(!refId.contains(':'))
        refId.prepend(langId + ":");

    if(m_knownContexts.keys().contains(id) && m_knownContexts.keys().contains(refId)) {
        *m_knownContexts[id]->context = *m_knownContexts[refId]->context;
        m_knownContexts[id]->styleId = m_knownContexts[refId]->styleId;
    }

    xml.readNext();
}

QRegularExpression LanguageLoader::resolveRegex(QString pattern, QRegularExpression::PatternOptions options, QString langId) {
    QString resultPattern = pattern;

    for (QString id : m_knownRegexes.keys()) {
        resultPattern = resultPattern.replace("\\%{" + id + "}", m_knownRegexes[id]);
    }
    resultPattern = resultPattern.replace("\\%[", m_languageLeftWordBoundary [langId]);
    resultPattern = resultPattern.replace("\\%]", m_languageRightWordBoundary[langId]);
    return QRegularExpression(resultPattern, options);
}

QString LanguageLoader::escapeNonExtended(QString pattern) {
    return pattern.replace('#', "\\#").replace(' ', "\\ ");
}

QString LanguageLoader::applyOptionsToSubRegex(QString pattern, QRegularExpression::PatternOptions options) {
    QString result = pattern;
    if((options & QRegularExpression::ExtendedPatternSyntaxOption) == 0)
        result = escapeNonExtended(result);
    if((options & QRegularExpression::CaseInsensitiveOption) != 0)
        result = result.prepend("(?:(?i)").append(")");
    else
        result = result.prepend("(?:(?-i)").append(")");
    return result;
}

void LanguageLoader::applyStyleToContext(QSharedPointer<LanguageContextReference> context, QString styleId) {
    context->styleId = styleId;
}
