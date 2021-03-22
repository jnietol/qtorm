/*
 * Copyright (C) 2019-2021 Dmitriy Purgin <dmitriy.purgin@sequality.at>
 * Copyright (C) 2019-2021 sequality software engineering e.U. <office@sequality.at>
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

#include "qormpropertymapping.h"

#include <QDebug>

QT_BEGIN_NAMESPACE

QDebug operator<<(QDebug dbg, const QOrmPropertyMapping& propertyMapping)
{
    QDebugStateSaver saver{dbg};

    dbg << "QOrmPropertyMapping(" << propertyMapping.classPropertyName() << " => "
        << propertyMapping.tableFieldName() << ", " << propertyMapping.dataType();

    if (propertyMapping.isAutogenerated())
        dbg << ", autogenerated";

    if (propertyMapping.isObjectId())
        dbg << ", object ID";

    if (propertyMapping.isTransient())
        dbg << ", transient";

    dbg << ")";

    return dbg;
}

class QOrmPropertyMappingPrivate : public QSharedData
{
    friend class QOrmPropertyMapping;

    QOrmPropertyMappingPrivate(const QOrmMetadata& enclosingEntity,
                               QMetaProperty qMetaProperty,
                               QString classPropertyName,
                               QString tableFieldName,
                               bool isObjectId,
                               bool isAutoGenerated,
                               QVariant::Type dataType,
                               const QOrmMetadata* referencedEntity,
                               bool isTransient,
                               QOrmUserMetadata userMetadata)
        : m_enclosingEntity{enclosingEntity}
        , m_qMetaProperty{std::move(qMetaProperty)}
        , m_classPropertyName{std::move(classPropertyName)}
        , m_tableFieldName{std::move(tableFieldName)}
        , m_isObjectId{isObjectId}
        , m_isAutogenerated{isAutoGenerated}
        , m_dataType{dataType}
        , m_referencedEntity{referencedEntity}
        , m_isTransient{isTransient}
        , m_userMetadata{std::move(userMetadata)}
    {
    }

    const QOrmMetadata& m_enclosingEntity;
    QMetaProperty m_qMetaProperty;
    QString m_classPropertyName;
    QString m_tableFieldName;
    bool m_isObjectId{false};
    bool m_isAutogenerated{false};
    QVariant::Type m_dataType{QVariant::Invalid};
    const QOrmMetadata* m_referencedEntity{nullptr};
    bool m_isTransient{false};
    QOrmUserMetadata m_userMetadata;
};

QOrmPropertyMapping::QOrmPropertyMapping(const QOrmMetadata& enclosingEntity,
                                         QMetaProperty qMetaProperty,
                                         QString classPropertyName,
                                         QString tableFieldName,
                                         bool isObjectId,
                                         bool isAutoGenerated,
                                         QVariant::Type dataType,
                                         const QOrmMetadata* referencedEntity,
                                         bool isTransient,
                                         QOrmUserMetadata userMetadata)
    : d{new QOrmPropertyMappingPrivate{enclosingEntity,
                                       std::move(qMetaProperty),
                                       std::move(classPropertyName),
                                       std::move(tableFieldName),
                                       isObjectId,
                                       isAutoGenerated,
                                       dataType,
                                       referencedEntity,
                                       isTransient,
                                       std::move(userMetadata)}}
{
}

QOrmPropertyMapping::QOrmPropertyMapping(const QOrmPropertyMapping&) = default;

QOrmPropertyMapping::QOrmPropertyMapping(QOrmPropertyMapping&&) = default;

QOrmPropertyMapping::~QOrmPropertyMapping() = default;

QOrmPropertyMapping& QOrmPropertyMapping::operator=(const QOrmPropertyMapping&) = default;

QOrmPropertyMapping& QOrmPropertyMapping::operator=(QOrmPropertyMapping&&) = default;

const QOrmMetadata& QOrmPropertyMapping::enclosingEntity() const
{
    return d->m_enclosingEntity;
}

const QMetaProperty& QOrmPropertyMapping::qMetaProperty() const
{
    return d->m_qMetaProperty;
}

QString QOrmPropertyMapping::classPropertyName() const
{
    return d->m_classPropertyName;
}

QString QOrmPropertyMapping::tableFieldName() const
{
    return d->m_tableFieldName;
}

bool QOrmPropertyMapping::isObjectId() const
{
    return d->m_isObjectId;
}

bool QOrmPropertyMapping::isAutogenerated() const
{
    return d->m_isAutogenerated;
}

QVariant::Type QOrmPropertyMapping::dataType() const
{
    return d->m_dataType;
}

QString QOrmPropertyMapping::dataTypeName() const
{
    return QString::fromUtf8(d->m_qMetaProperty.typeName());
}

bool QOrmPropertyMapping::isReference() const
{
    return d->m_referencedEntity != nullptr;
}

const QOrmMetadata* QOrmPropertyMapping::referencedEntity() const
{
    return d->m_referencedEntity;
}

bool QOrmPropertyMapping::isTransient() const
{
    return d->m_isTransient;
}

const QOrmUserMetadata& QOrmPropertyMapping::userMetadata() const
{
    return d->m_userMetadata;
}

QT_END_NAMESPACE
