/*
 * Copyright (C) 2016-2017 Fanout, Inc.
 *
 * This file is part of Pushpin.
 *
 * Pushpin is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Pushpin is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "filter.h"

#include "log.h"
#include "idformat.h"

namespace {

class SkipSelfFilter : public Filter
{
public:
	SkipSelfFilter() :
		Filter("skip-self")
	{
	}

	virtual SendAction sendAction() const
	{
		QString user = context().subscriptionMeta.value("user");
		QString sender = context().publishMeta.value("sender");
		if(!user.isEmpty() && !sender.isEmpty() && sender == user)
			return Drop;

		return Send;
	}
};

class SkipUsersFilter : public Filter
{
public:
	SkipUsersFilter() :
		Filter("skip-users")
	{
	}

	virtual SendAction sendAction() const
	{
		QString user = context().subscriptionMeta.value("user");

		QStringList skip_users;
		foreach(const QString &part, context().publishMeta.value("skip_users").split(','))
		{
			QString s = part.trimmed();
			if(!s.isEmpty())
				skip_users += s;
		}

		if(!user.isEmpty() && skip_users.contains(user))
			return Drop;

		return Send;
	}
};

class RequireSubFilter : public Filter
{
public:
	RequireSubFilter() :
		Filter("require-sub")
	{
	}

	virtual SendAction sendAction() const
	{
		QString require_sub = context().publishMeta.value("require_sub");
		if(!require_sub.isEmpty() && !context().prevIds.keys().contains(require_sub))
			return Drop;

		return Send;
	}
};

class BuildIdFilter : public Filter
{
public:
	IdFormat::ContentRenderer *idContentRenderer;

	BuildIdFilter() :
		Filter("build-id"),
		idContentRenderer(0)
	{
	}

	~BuildIdFilter()
	{
		delete idContentRenderer;
	}

	bool ensureInit()
	{
		if(!idContentRenderer)
		{
			QString idFormat = context().subscriptionMeta.value("id_format");
			if(idFormat.isNull())
			{
				setError("no sub meta 'id_format'");
				return false;
			}

			QHash<QString, QByteArray> idTemplateVars;
			QHashIterator<QString, QString> it(context().prevIds);
			while(it.hasNext())
			{
				it.next();
				idTemplateVars.insert(it.key(), it.value().toUtf8());
			}

			QString _error;
			QByteArray id;

			if(!idTemplateVars.isEmpty())
			{
				id = IdFormat::renderId(idFormat.toUtf8(), idTemplateVars, &_error);
				if(id.isNull())
				{
					setError(QString("failed to render ID: %1").arg(_error));
					return false;
				}
			}

			bool hex = false;

			QString idEncoding = context().subscriptionMeta.value("id_encoding");
			if(!idEncoding.isNull())
			{
				if(idEncoding == "hex")
				{
					hex = true;
				}
				else
				{
					setError(QString("unsupported encoding: %1").arg(idEncoding));
					return false;
				}
			}

			idContentRenderer = new IdFormat::ContentRenderer(id, hex);
		}

		return true;
	}

	virtual QByteArray update(const QByteArray &data)
	{
		if(!ensureInit())
			return QByteArray();

		QByteArray buf = idContentRenderer->update(data);
		if(buf.isNull())
		{
			setError(idContentRenderer->errorMessage());
			return QByteArray();
		}

		return buf;
	}

	virtual QByteArray finalize()
	{
		if(!ensureInit())
			return QByteArray();

		QByteArray buf = idContentRenderer->finalize();
		if(buf.isNull())
		{
			setError(idContentRenderer->errorMessage());
			return QByteArray();
		}

		return buf;
	}
};

}

Filter::Filter(const QString &name) :
	name_(name)
{
}

Filter::~Filter()
{
}

Filter::SendAction Filter::sendAction() const
{
	return Send;
}

QByteArray Filter::update(const QByteArray &data)
{
	return data;
}

QByteArray Filter::finalize()
{
	return QByteArray("");
}

QByteArray Filter::process(const QByteArray &data)
{
	QByteArray out = update(data);
	if(out.isNull())
		return QByteArray();

	QByteArray buf = finalize();
	if(buf.isNull())
		return QByteArray();

	return out + buf;
}

Filter *Filter::create(const QString &name)
{
	if(name == "skip-self")
		return new SkipSelfFilter;
	else if(name == "skip-users")
		return new SkipUsersFilter;
	else if(name == "require-sub")
		return new RequireSubFilter;
	else if(name == "build-id")
		return new BuildIdFilter;
	else
		return 0;
}

QStringList Filter::names()
{
	return (QStringList()
		<< "skip-self"
		<< "skip-users"
		<< "require-sub"
		<< "build-id");
}
