/*
 * Copyright (C) 2019-2024 Dmitriy Purgin <dpurgin@gmail.com>
 * Copyright (C) 2019-2024 Dmitriy Purgin <dmitriy.purgin@sequality.at>
 * Copyright (C) 2019-2024 sequality software engineering e.U. <office@sequality.at>
 *
 * This file is part of QtOrm library.
 *
 * QtOrm is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * QtOrm is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with QtOrm.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "qormmetadatacache.h"
#include "qormglobal_p.h"
#include "qormmetadata_p.h"
#include "qormpropertymapping.h"

#include <QtCore/qhash.h>
#include <QtCore/qloggingcategory.h>
#include <QtCore/qmetaobject.h>
#include <QtCore/qset.h>
#include <QtCore/qvector.h>

#include <optional>

namespace
{
    struct KeywordDescriptor
    {
        QOrm::Keyword id;
        QLatin1String token;
    };

    struct KeywordPosition
    {
        int pos{-1};
        const KeywordDescriptor* keyword{nullptr};
    };

    const KeywordDescriptor ClassKeywords[] = {{QOrm::Keyword::Table, QLatin1String("TABLE")},
                                               {QOrm::Keyword::Schema, QLatin1String("SCHEMA")}};
    const KeywordDescriptor PropertyKeywords[] = {
        {QOrm::Keyword::Column, QLatin1String("COLUMN")},
        {QOrm::Keyword::Identity, QLatin1String("IDENTITY")},
        {QOrm::Keyword::Transient, QLatin1String("TRANSIENT")},
        {QOrm::Keyword::Autogenerated, QLatin1String("AUTOGENERATED")}};

    template<typename Iterable>
    KeywordPosition findNextKeyword(const QString& data,
                                    int startFrom,
                                    Iterable&& keywordDescriptors)
    {
        for (int i = startFrom; i < data.size(); ++i)
        {
            if (data[i].isUpper())
            {
                for (auto it = std::cbegin(std::forward<Iterable>(keywordDescriptors));
                     it != std::cend(std::forward<Iterable>(keywordDescriptors));
                     ++it)
                {
                    if (data.midRef(i, it->token.size()) == it->token)
                    {
                        return {i, it};
                    }
                }
            }
        }

        return {-1, nullptr};
    }

    template<typename T>
    struct ExtractResult
    {
        T value;
        KeywordPosition nextKeyword;
    };

    template<typename T>
    [[nodiscard]] auto extracted(T&& value, KeywordPosition nextKeyword)
    {
        return ExtractResult<T>{std::forward<T>(value), nextKeyword};
    }

    template<typename Iterable>
    [[nodiscard]] ExtractResult<QString> extractString(const QString& data,
                                                       int pos,
                                                       Iterable&& keywordDescriptors)
    {
        auto nextKeyword = findNextKeyword(data, pos, std::forward<Iterable>(keywordDescriptors));
        int len = nextKeyword.pos == -1 ? data.size() - pos : nextKeyword.pos - pos;

        return extracted(data.mid(pos, len), nextKeyword);
    }

    template<typename Iterable>
    [[nodiscard]] std::optional<ExtractResult<std::optional<bool>>>
    extractBoolean(const QString& data, int pos, Iterable&& keywordDescriptors)
    {
        auto [str, nextKeyword] =
            extractString(data, pos, std::forward<Iterable>(keywordDescriptors));

        if (str == QLatin1String("true"))
        {
            return extracted(std::make_optional(true), nextKeyword);
        }
        else if (str == QLatin1String("false"))
        {
            return extracted(std::make_optional(false), nextKeyword);
        }
        else if (str.isEmpty())
        {
            return extracted(std::optional<bool>{std::nullopt}, nextKeyword);
        }
        else
        {
            return std::nullopt;
        }
    }

    [[nodiscard]] QOrmUserMetadata extractClassInfo(const QMetaObject& qMetaObject, QString data)
    {
        // Q_CLASSINFO removes whitespaces from its arguments. So a declaration like
        //    Q_ORM_CLASS(TABLE Province SCHEMA update)
        // turns into "TABLEProvinceSCHEMAupdate".

        QOrmUserMetadata ormClassInfo;

        auto keywordPosition = findNextKeyword(data, 0, ClassKeywords);
        if (keywordPosition.pos != 0)
        {
            qFatal("QtOrm: syntax error in %s: Q_ORM_CLASS() must begin with a keyword",
                   qMetaObject.className());
        }

        while (keywordPosition.pos != -1)
        {
            int pos = keywordPosition.pos + keywordPosition.keyword->token.size();

            if (keywordPosition.keyword->id == QOrm::Keyword::Table)
            {
                auto extractResult = extractString(data, pos, ClassKeywords);

                QString tableName = extractResult.value;
                keywordPosition = extractResult.nextKeyword;

                if (!tableName.isEmpty())
                {
                    ormClassInfo.insert(QOrm::Keyword::Table, tableName);
                }
                else
                {
                    qFatal("QtOrm: syntax error in %s: Q_ORM_CLASS(TABLE <table name>) "
                           "requires a string argument.",
                           qMetaObject.className());
                }
            }
            else if (keywordPosition.keyword->id == QOrm::Keyword::Schema)
            {
                auto extractResult = extractString(data, pos, ClassKeywords);

                QString schemaMode = extractResult.value;
                keywordPosition = extractResult.nextKeyword;

                if (!schemaMode.isEmpty())
                {
                    ormClassInfo.insert(QOrm::Keyword::Schema, schemaMode);
                }
                else
                {
                    qFatal("QtOrm: syntax error in %s: Q_ORM_CLASS(SCHEMA <schema mode>) requires "
                           "a string argument.",
                           qMetaObject.className());
                }
            }
        }

        return ormClassInfo;
    }

    [[nodiscard]] QOrmUserMetadata extractPropertyInfo(const QMetaObject& qMetaObject, QString data)
    {
        // Q_CLASSINFO removes whitespaces from its arguments. So a declaration like
        //    Q_ORM_PROPERTY(id COLUMN province_id AUTOGENERATED IDENTITY)
        // turns into "idCOLUMNprovince_idAUTOGENERATEDIDENTITY".
        //
        // First, try to find the first keyword in the string. Everything prior to its position is
        // considered to be a property name.

        auto keywordPosition = findNextKeyword(data, 0, PropertyKeywords);

        if (keywordPosition.pos == -1)
        {
            qFatal("QtOrm: syntax error in %s: cannot find any QtOrm keywords in a Q_ORM_PROPERTY",
                   qMetaObject.className());
        }

        QString propertyName = data.mid(0, keywordPosition.pos);

        QOrmUserMetadata ormPropertyInfo;
        ormPropertyInfo.insert(QOrm::Keyword::Property, propertyName);

        // Check that the property exists in the metadata.

        int i = 0;
        for (; i < qMetaObject.propertyCount() && qMetaObject.property(i).name() != propertyName;
             ++i)
            ;

        if (i == qMetaObject.propertyCount())
        {
            qFatal("QtOrm: Q_ORM_PROPERTY(%s ...) does not have a corresponding Q_PROPERTY(%s ...) "
                   "in %s",
                   qPrintable(propertyName),
                   qPrintable(propertyName),
                   qMetaObject.className());
        }

        // Evaluate the current keyword and read its argument from the string until the next keyword
        // or the end of the string.

        while (keywordPosition.pos != -1)
        {
            int pos = keywordPosition.pos + keywordPosition.keyword->token.size();

            if (keywordPosition.keyword->id == QOrm::Keyword::Column)
            {
                auto extractResult = extractString(data, pos, PropertyKeywords);

                QString columnName = extractResult.value;
                keywordPosition = extractResult.nextKeyword;

                if (!columnName.isEmpty())
                {
                    ormPropertyInfo.insert(QOrm::Keyword::Column, columnName);
                }
                else
                {
                    qFatal("QtOrm: syntax error in %s: Q_ORM_PROPERTY(%s COLUMN <column name>) "
                           "requires a string argument.",
                           qMetaObject.className(),
                           qPrintable(propertyName));
                }
            }
            else if (keywordPosition.keyword->id == QOrm::Keyword::Identity)
            {
                auto extractResult = extractBoolean(data, pos, PropertyKeywords);

                if (!extractResult.has_value())
                {
                    qFatal("QtOrm: syntax error in %s in Q_ORM_PROPERTY(%s ...) after IDENTITY",
                           qMetaObject.className(),
                           qPrintable(propertyName));
                }

                std::optional<bool> isIdentity = extractResult->value;
                keywordPosition = extractResult->nextKeyword;

                ormPropertyInfo.insert(QOrm::Keyword::Identity, isIdentity.value_or(true));
            }
            else if (keywordPosition.keyword->id == QOrm::Keyword::Transient)
            {
                auto extractResult = extractBoolean(data, pos, PropertyKeywords);

                if (!extractResult.has_value())
                {
                    qFatal("QtOrm: syntax error in %s in Q_ORM_PROPERTY(%s ...) after TRANSIENT",
                           qMetaObject.className(),
                           qPrintable(propertyName));
                }

                std::optional<bool> isTransient = extractResult->value;
                keywordPosition = extractResult->nextKeyword;

                ormPropertyInfo.insert(QOrm::Keyword::Transient, isTransient.value_or(true));
            }
            else if (keywordPosition.keyword->id == QOrm::Keyword::Autogenerated)
            {
                auto extractResult = extractBoolean(data, pos, PropertyKeywords);

                if (!extractResult.has_value())
                {
                    qFatal(
                        "QtOrm: syntax error in %s in Q_ORM_PROPERTY(%s ...) after AUTOGENERATED",
                        qMetaObject.className(),
                        qPrintable(propertyName));
                }

                std::optional<bool> isAutogenerated = extractResult->value;
                keywordPosition = extractResult->nextKeyword;

                ormPropertyInfo.insert(QOrm::Keyword::Autogenerated,
                                       isAutogenerated.value_or(true));
            }
        }

        return ormPropertyInfo;
    }
} // namespace

class QOrmMetadataCachePrivate
{
    friend class QOrmMetadataCache;

    struct MappingDescriptor
    {
        QString classPropertyName;
        QString tableFieldName;
        bool isObjectId = false;
        bool isAutogenerated = false;
        const QOrmMetadata* referencedEntity = nullptr;
        bool isTransient = false;
        bool isEnumeration{false};
        QVariant::Type dataType{QVariant::Invalid};
    };

    std::unordered_map<QByteArray, QOrmMetadata> m_cache;

    QSet<QByteArray> m_underConstruction;
    QSet<QByteArray> m_constructed;

    [[nodiscard]] const QOrmMetadata& get(const QMetaObject& metaObject);

    void initialize(const QByteArray& className, const QMetaObject& qMetaObject);

    [[nodiscard]] MappingDescriptor mappingDescriptor(const QMetaObject& qMetaObject,
                                                      const QMetaProperty& property,
                                                      const QOrmUserMetadata& userPropertyMetadata);

    void validateConstructor(const QMetaObject& qMetaObject);

    template<typename Container>
    void validateCrossReferences(Container&& entityNames);
};

const QOrmMetadata& QOrmMetadataCachePrivate::get(const QMetaObject& qMetaObject)
{
    QByteArray className{qMetaObject.className()};

    if (m_cache.find(className) == std::end(m_cache))
    {
        initialize(className, qMetaObject);
    }

    return m_cache.at(className);
}

void QOrmMetadataCachePrivate::initialize(const QByteArray& className,
                                          const QMetaObject& qMetaObject)
{
    m_underConstruction.insert(className);

    // check whether the entity is creatable
    validateConstructor(qMetaObject);

    QOrmMetadataPrivate* data = new QOrmMetadataPrivate{qMetaObject};

    // cache the object before its actual initialization to resolve cyclic references inside the
    // mappings
    m_cache.emplace(className, QOrmMetadata{data});

    QOrmUserMetadata ormClassInfo;
    QHash<QString, QOrmUserMetadata> ormPropertyInfo;

    for (int i = 0; i < qMetaObject.classInfoCount(); ++i)
    {
        QMetaClassInfo qtClassInfo = qMetaObject.classInfo(i);

        if (qstrcmp(qtClassInfo.name(), "QtOrmClassInfo") == 0)
        {
            if (!ormClassInfo.isEmpty())
            {
                qFatal("QtOrm: %s has more than one Q_ORM_CLASS() entries.",
                       qMetaObject.className());
            }

            ormClassInfo = extractClassInfo(qMetaObject, qtClassInfo.value());
        }
        else if (qstrcmp(qtClassInfo.name(), "QtOrmPropertyInfo") == 0)
        {
            QOrmUserMetadata propertyInfo = extractPropertyInfo(qMetaObject, qtClassInfo.value());

            QString propertyName = propertyInfo.value(QOrm::Keyword::Property, "").toString();

            if (propertyName.isEmpty())
            {
                qFatal("QtOrm: %s has a Q_ORM_PROPERTY() with an undefined property name.",
                       qMetaObject.className());
            }

            if (ormPropertyInfo.contains(propertyName))
            {
                qFatal("QtOrm: %s has more than one Q_ORM_PROPERTY(%s ...) entries",
                       qMetaObject.className(),
                       qPrintable(propertyName));
            }

            ormPropertyInfo.insert(propertyName, propertyInfo);
        }
    }

    // initialize the private part of the cached object
    data->m_className = QString::fromUtf8(qMetaObject.className());
    data->m_tableName =
        ormClassInfo.value(QOrm::Keyword::Table, QVariant::fromValue(data->m_className)).toString();
    data->m_userMetadata = ormClassInfo;

    for (int i = 0; i < qMetaObject.propertyCount(); ++i)
    {
        QMetaProperty property = qMetaObject.property(i);

        // skip all properties of QObject
        if (property.enclosingMetaObject() == &QObject::staticMetaObject)
            continue;

        QString propertyName = QString::fromUtf8(property.name());
        QOrmUserMetadata userPropertyMetadata =
            ormPropertyInfo.value(propertyName, QOrmUserMetadata{});

        MappingDescriptor descriptor =
            mappingDescriptor(qMetaObject, property, userPropertyMetadata);

        if (!descriptor.isTransient &&
            (!property.isReadable() || !property.isWritable() || !property.hasNotifySignal() ||
             !property.notifySignal().isValid()))
        {
            qFatal("QtOrm: The property %s::%s must have READ, WRITE, and NOTIFY declarations in "
                   "Q_PROPERTY().",
                   className.data(),
                   property.name());
        }

        if (descriptor.isTransient && descriptor.isObjectId)
        {
            qFatal("QtOrm: The property %s::%s cannot be marked TRANSIENT and IDENTITY at the same "
                   "time.",
                   qPrintable(className),
                   property.name());
        }

        if (descriptor.isAutogenerated && !descriptor.isObjectId)
        {
            qFatal("QtOrm: The property %s::%s cannot be marked AUTOGENERATED without IDENTITY.",
                   qPrintable(className),
                   property.name());
        }

        data->m_propertyMappings.emplace_back(m_cache.at(className),
                                              property,
                                              descriptor.classPropertyName,
                                              descriptor.tableFieldName,
                                              descriptor.isObjectId,
                                              descriptor.isAutogenerated,
                                              descriptor.dataType,
                                              descriptor.referencedEntity,
                                              descriptor.isTransient,
                                              userPropertyMetadata);
        auto idx = static_cast<int>(data->m_propertyMappings.size() - 1);

        data->m_classPropertyMappingIndex.insert(descriptor.classPropertyName, idx);
        data->m_tableFieldMappingIndex.insert(descriptor.tableFieldName, idx);

        if (descriptor.isObjectId)
            data->m_objectIdPropertyMappingIdx = idx;
    }

    m_underConstruction.remove(className);
    m_constructed.insert(className);

    // After all entity metadata is gathered, check their cross-reference consistency
    if (m_underConstruction.empty())
        validateCrossReferences(m_constructed);
}

QOrmMetadataCachePrivate::MappingDescriptor QOrmMetadataCachePrivate::mappingDescriptor(
    const QMetaObject& qMetaObject,
    const QMetaProperty& property,
    const QOrmUserMetadata& userPropertyMetadata)
{
    Q_ASSERT(userPropertyMetadata.value(QOrm::Keyword::Property, property.name()) ==
             property.name());

    MappingDescriptor descriptor;

    QString tableFieldName = QString::fromUtf8(property.name()).toLower();
    bool isObjectId = qstricmp(property.name(), "id") == 0;
    bool isAutogenerated = isObjectId;
    bool isTransient = !property.isStored();

    // Check if defaults are overridden by the user property metadata
    if (userPropertyMetadata.contains(QOrm::Keyword::Column))
    {
        tableFieldName = userPropertyMetadata.value(QOrm::Keyword::Column).toString();
        Q_ASSERT(!tableFieldName.isEmpty());
    }

    if (userPropertyMetadata.contains(QOrm::Keyword::Identity))
    {
        isObjectId = userPropertyMetadata.value(QOrm::Keyword::Identity).toBool();
    }

    if (userPropertyMetadata.contains(QOrm::Keyword::Autogenerated))
    {
        isAutogenerated = userPropertyMetadata.value(QOrm::Keyword::Autogenerated).toBool();
    }

    if (userPropertyMetadata.contains(QOrm::Keyword::Transient))
    {
        isTransient = userPropertyMetadata.value(QOrm::Keyword::Transient).toBool();
    }

    descriptor.classPropertyName = QString::fromUtf8(property.name());
    descriptor.tableFieldName = tableFieldName;
    descriptor.isObjectId = isObjectId;
    descriptor.isAutogenerated = isAutogenerated;
    descriptor.isTransient = isTransient;
    descriptor.dataType = property.type();

    // Check if this is one-to-many or many-to-one relation.
    // One-to-many relation will have a container in type. If so, extract the contained
    // type.
    if (property.type() == QVariant::UserType)
    {
        auto typeName = QByteArray{property.typeName()};

        static auto aggregatePrefixes = QVector<QByteArray>{"QVector<", "QSet<"};

        for (const auto& prefix : aggregatePrefixes)
        {
            if (typeName.startsWith(prefix))
            {
                typeName = typeName.mid(prefix.length());
                break;
            }
        }

        if (typeName.endsWith("*>"))
        {
            typeName.chop(1);
            descriptor.tableFieldName.clear();
            descriptor.isTransient = true;
        }
        else
        {
            descriptor.isTransient = false;
        }

        if (property.userType() == QMetaType::UnknownType)
        {
            if (typeName.endsWith('*'))
                typeName.chop(1);

            qFatal("QtOrm: An unregistered type %s is used in %s::%s.\n"
                   "1. If this is a referenced entity, it must be registered with "
                   "qRegisterOrmEntity<%s>().\n"
                   "2. If this is a type alias for a container type, this is currently not "
                   "supported.\n"
                   "3. If this is an enumeration type, it must be fully qualified, registered with "
                   "Q_DECLARE_METATYPE(%s) and qRegisterOrmEnum<%s>().",
                   typeName.data(),
                   qMetaObject.className(),
                   property.name(),
                   typeName.data(),
                   typeName.data(),
                   typeName.data());
        }

        QMetaType::TypeFlags flags = QMetaType::typeFlags(QMetaType::type(typeName));

        if (flags.testFlag(QMetaType::PointerToQObject))
        {
            if (!userPropertyMetadata.contains(QOrm::Keyword::Column) && !descriptor.isTransient)
            {
                descriptor.tableFieldName += "_id";
            }

            const QMetaObject* referencedMeta =
                QMetaType::metaObjectForType(QMetaType::type(typeName));

            if (referencedMeta == nullptr)
            {
                qFatal("QtOrm: Cannot deduce ORM entity from type %s used in %s::%s",
                       property.typeName(),
                       qMetaObject.className(),
                       property.name());
            }
            descriptor.referencedEntity = &get(*referencedMeta);
            Q_ASSERT(descriptor.referencedEntity != nullptr);
        }
        else if (flags.testFlag(QMetaType::IsEnumeration))
        {
            descriptor.isEnumeration = true;
            descriptor.dataType = QVariant::Int;
        }
    }

    return descriptor;
}

void QOrmMetadataCachePrivate::validateConstructor(const QMetaObject& qMetaObject)
{
    bool hasError = false;

    if (qMetaObject.constructorCount() == 0)
    {
        hasError = true;
    }

    for (int i = 0; i < qMetaObject.constructorCount() && !hasError; ++i)
    {
        QMetaMethod ctor = qMetaObject.constructor(i);

        hasError = (ctor.access() != QMetaMethod::Public) || (ctor.parameterCount() > 1) ||
                   (ctor.parameterCount() == 1 && ctor.parameterTypes().front() != "QObject*");
    }

    if (hasError)
    {
        qFatal("QtOrm: Entity %s requires a metaobject-invokable public default constructor, "
               "e.g.: Q_INVOKABLE explicit %s(QObject* parent = nullptr): QObject{parent} {}",
               qMetaObject.className(),
               qMetaObject.className());
    }
}

template<typename Container>
void QOrmMetadataCachePrivate::validateCrossReferences(Container&& entityNames)
{
    for (const QByteArray& entityClassName : entityNames)
    {
        Q_ASSERT(m_cache.find(entityClassName) != std::end(m_cache));

        for (const QOrmPropertyMapping& mapping : m_cache.at(entityClassName).propertyMappings())
        {
            if (!mapping.isReference())
                continue;

            // If the property is a container of referenced entities, check that there is a
            // corresponding pointer on the other side of the relation
            // E.g.: if entity A has a property QVector<B*>, then the entity B should have a
            // property A*.
            if (mapping.isTransient())
            {
                const auto& referencedMappings = mapping.referencedEntity()->propertyMappings();

                auto it = std::find_if(std::cbegin(referencedMappings),
                                       std::cend(referencedMappings),
                                       [entityClassName](const QOrmPropertyMapping& mapping) {
                                           // require pointer to this entity
                                           return mapping.isReference() &&
                                                  mapping.referencedEntity()->className() ==
                                                      entityClassName;
                                       });

                if (it == std::cend(referencedMappings))
                {
                    qFatal("QtOrm: Entity %s referenced in %s::%s must have a back-reference "
                           "to %s. "
                           "Declare a Q_PROPERTY(%s* ...) in %s.",
                           mapping.referencedEntity()->className().toUtf8().data(),
                           entityClassName.data(),
                           mapping.classPropertyName().toUtf8().data(),
                           entityClassName.data(),
                           entityClassName.data(),
                           mapping.referencedEntity()->className().toUtf8().data());
                }
            }
            // Many-to-one relations. Check that the related entity has object ID
            else if (mapping.referencedEntity()->objectIdMapping() == nullptr)
            {
                qFatal("QtOrm: Entity %s referenced in %s::%s must have an object ID property",
                       mapping.referencedEntity()->className().toUtf8().data(),
                       entityClassName.data(),
                       mapping.classPropertyName().toUtf8().data());
            }
        }
    }
}

QOrmMetadataCache::QOrmMetadataCache()
    : d{new QOrmMetadataCachePrivate}
{
}

QOrmMetadataCache::QOrmMetadataCache(QOrmMetadataCache&&) = default;

QOrmMetadataCache& QOrmMetadataCache::operator=(QOrmMetadataCache&&) = default;

QOrmMetadataCache::~QOrmMetadataCache() = default;

const QOrmMetadata& QOrmMetadataCache::operator[](const QMetaObject& qMetaObject)
{
    return d->get(qMetaObject);
}
