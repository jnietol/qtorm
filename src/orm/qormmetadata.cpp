#include "qormmetadata.h"
#include "qormglobal_p.h"

#include <QDebug>
#include <QMetaProperty>

QT_BEGIN_NAMESPACE

class QOrmMetadataPrivate : public QSharedData
{
    friend class QOrmMetadata;

    QOrmMetadataPrivate(const QMetaObject& metaObject, QOrmMetadata* parent);

    const QMetaObject& m_qMetaObject;

    QString m_className;
    QString m_tableName;
    std::vector<QOrmPropertyMapping> m_propertyMappings;

    int m_objectIdPropertyMappingIdx{-1};
    QHash<QString, int> m_classPropertyMappingIndex;
    QHash<QString, int> m_tableFieldMappingIndex;
};

QOrmMetadataPrivate::QOrmMetadataPrivate(const QMetaObject& qMetaObject, QOrmMetadata* parent)
    : m_qMetaObject{qMetaObject}
{
    Q_ASSERT(parent != nullptr);

    m_className = QString::fromUtf8(qMetaObject.className());
    m_tableName = QString::fromUtf8(qMetaObject.className());

    for (int i = 0; i < qMetaObject.propertyCount(); ++i)
    {
        QMetaProperty property = qMetaObject.property(i);

        // skip all properties of QObject
        if (property.enclosingMetaObject() == &QObject::staticMetaObject)
            continue;

        auto classPropertyName = QString::fromUtf8(property.name());
        auto tableFieldName = QString::fromUtf8(property.name());
        bool isObjectId = QLatin1String("id") == property.name();
        bool isAutogenerated = isObjectId;

        m_propertyMappings.emplace_back(*parent,
                                        classPropertyName,
                                        tableFieldName,
                                        isObjectId,
                                        isAutogenerated,
                                        property.type());
        int idx = static_cast<int>(m_propertyMappings.size() - 1);

        m_classPropertyMappingIndex.insert(classPropertyName, idx);
        m_tableFieldMappingIndex.insert(tableFieldName, idx);

        if (isObjectId)
            m_objectIdPropertyMappingIdx = idx;
    }
}

QOrmMetadata::QOrmMetadata(const QMetaObject& qMetaObject)
    : d{new QOrmMetadataPrivate{qMetaObject, this}}
{
}
QOrmMetadata::QOrmMetadata(const QOrmMetadata&) = default;

QOrmMetadata::QOrmMetadata(QOrmMetadata&&) = default;

QOrmMetadata::~QOrmMetadata() = default;

QOrmMetadata& QOrmMetadata::operator=(const QOrmMetadata&) = default;

QOrmMetadata& QOrmMetadata::operator=(QOrmMetadata&&) = default;

const QMetaObject& QOrmMetadata::qMetaObject() const
{
    return d->m_qMetaObject;
}

QString QOrmMetadata::className() const
{
    return d->m_className;
}

QString QOrmMetadata::tableName() const
{
    return d->m_tableName;
}

const std::vector<QOrmPropertyMapping>& QOrmMetadata::propertyMappings() const
{
    return d->m_propertyMappings;
}

const QOrmPropertyMapping* QOrmMetadata::tableFieldMapping(const QString& fieldName) const
{
    auto it = d->m_tableFieldMappingIndex.find(fieldName);

    if (it == std::end(d->m_tableFieldMappingIndex))
        return nullptr;

    return &d->m_propertyMappings[it.value()];
}

const QOrmPropertyMapping* QOrmMetadata::classPropertyMapping(const QString& classProperty) const
{
    auto it = d->m_classPropertyMappingIndex.find(classProperty);

    if (it == std::end(d->m_classPropertyMappingIndex))
        return nullptr;

    return &d->m_propertyMappings[it.value()];
}

const QOrmPropertyMapping* QOrmMetadata::objectIdMapping() const
{
    return d->m_objectIdPropertyMappingIdx == -1
               ? nullptr
               : &d->m_propertyMappings[d->m_objectIdPropertyMappingIdx];
}

QDebug operator<<(QDebug dbg, const QOrmMetadata& metadata)
{
    QDebugStateSaver saver{dbg};
    dbg.nospace().nospace() << "QOrmMetadata("
                            << metadata.className() << " => " << metadata.tableName()
                            << ")";
    return dbg;
}

QT_END_NAMESPACE
