#include "qormqueryresult.h"

#include "qormerror.h"

QT_BEGIN_NAMESPACE

class QOrmQueryResultPrivate : public QSharedData
{
    friend class QOrmQueryResult;

    QOrmQueryResultPrivate(const QOrmError& error, const QVector<QObject*>& resultSet)
        : m_error{error},
          m_resultSet{resultSet}
    {
    }

    QOrmError m_error;
    QVector<QObject*> m_resultSet;
};

// Primary ctor
QOrmQueryResult::QOrmQueryResult(const QOrmError& error,
                                 const QVector<QObject*>& resultSet)
    : d{new QOrmQueryResultPrivate{error, resultSet}}
{

}

QOrmQueryResult::QOrmQueryResult()
    : QOrmQueryResult{QOrmError{QOrm::Error::None, ""}}
{
}

QOrmQueryResult::~QOrmQueryResult() = default;

QOrmQueryResult::QOrmQueryResult(const QOrmQueryResult&) = default;

#ifdef Q_COMPILER_RVALUE_REFS
QOrmQueryResult::QOrmQueryResult(QOrmQueryResult&&) = default;
#endif

QOrmQueryResult& QOrmQueryResult::operator=(const QOrmQueryResult&) = default;

#ifdef Q_COMPILER_RVALUE_REFS
QOrmQueryResult& QOrmQueryResult::operator=(QOrmQueryResult&&) = default;
#endif

QOrmQueryResult::QOrmQueryResult(const QOrmError& error)
    : QOrmQueryResult{error, {}}
{
}

QOrmQueryResult::QOrmQueryResult(const QVector<QObject*>& resultSet)
    : QOrmQueryResult{QOrmError{QOrm::Error::None, ""}, resultSet}
{
}

QOrmError QOrmQueryResult::error() const
{
    return d->m_error;
}

QVector<QObject*> QOrmQueryResult::toVector() const
{
    return d->m_resultSet;
}

QT_END_NAMESPACE
