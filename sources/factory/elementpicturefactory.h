/*
	Copyright 2006-2026 The QElectroTech Team
	This file is part of QElectroTech.

	QElectroTech is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	QElectroTech is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with QElectroTech.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef ELEMENTPICTUREFACTORY_H
#define ELEMENTPICTUREFACTORY_H

#include <QColor>
#include <QMutex>
#include <QSharedPointer>
#include <QHash>
#include <QMap>
#include <QMultiHash>
#include <QPair>
#include <QString>
#include <QTransform>

class ElementsLocation;
class QPicture;
class QUuid;
class QDomElement;
class QPainter;
class QGraphicsSimpleTextItem;

/**
	@brief The ElementPictureFactory class
	This class is singleton factory, use
	to create and get the picture use by elements
*/
class ElementPictureFactory
{
	public :
		struct primitives
		{
			QList<QLineF> m_lines;
			QList<QRectF> m_rectangles;
			QList<QRectF> m_circles;
			QList<QVector<QPointF>> m_polygons;
			QList<QVector<qreal>> m_arcs;
			QList<QGraphicsSimpleTextItem*> m_texts;
		};
		
		
		/**
			@brief instance
			@return The instance of the factory
		*/
		static ElementPictureFactory* instance()
		{
			static QMutex mutex;
			if (!m_factory)
			{
				mutex.lock();
				if (!m_factory) {
					m_factory = new ElementPictureFactory();	
				}
				mutex.unlock();
			}
			return m_factory;
		}
		
		/**
			@brief dropInstance
			Drop the instance of factory
		*/
		static void dropInstance()
		{
			static QMutex mutex;
			if (m_factory)
			{
				mutex.lock();
				delete m_factory;
				m_factory = nullptr;
				mutex.unlock();
			}
		}

			/// The choices of a placed element among the options of its
			/// symbol (ElementOptions::Choices): none draws the defaults
		using Choices = QMap<QString, QString>;
			/// The entry of the choices which draws the symbol in another
			/// colour than black (a colour name): only the ink changes, the
			/// white that masks and the colours chosen in the symbol stay
		static QString inkChoice() { return QStringLiteral("__ink"); }

		void getPictures(const ElementsLocation &location, QPicture &picture, QPicture &low_picture,
						 const Choices &choices = Choices());
		void getReadablePictures(const ElementsLocation &location, const QTransform &texts_transform,
								 QPicture &picture, QPicture &low_picture,
								 const Choices &choices = Choices());
		QPixmap pixmap(const ElementsLocation &location);
		ElementPictureFactory::primitives getPrimitives(const ElementsLocation &location,
														const Choices &choices = Choices());
		void dropCache(const ElementsLocation &location);
		QPicture pictureFromDefinition(const QDomElement &definition,
									   const Choices &choices = Choices()) const;

	private:
		ElementPictureFactory() {}
		ElementPictureFactory (const ElementPictureFactory &);
		ElementPictureFactory operator= (const ElementPictureFactory &);
		~ElementPictureFactory();

		static QUuid cacheKey(const ElementsLocation &location, const Choices &choices = Choices());
		static int readableKey(const QTransform &texts_transform);
		bool build(const ElementsLocation &location, QPicture *picture=nullptr, QPicture *low_picture=nullptr,
				   const QTransform &texts_transform=QTransform(), const Choices &choices = Choices());
		void dropKey(const QUuid &uuid);
		void parseElement(const QDomElement &dom, QPainter &painter, primitives &prim) const;
		void parseLine   (const QDomElement &dom, QPainter &painter, primitives &prim) const;
		void parseRect   (const QDomElement &dom, QPainter &painter, primitives &prim) const;
		void parseEllipse(const QDomElement &dom, QPainter &painter, primitives &prim) const;
		void parseCircle (const QDomElement &dom, QPainter &painter, primitives &prim) const;
		void parseArc    (const QDomElement &dom, QPainter &painter, primitives &prim) const;
		void parsePolygon(const QDomElement &dom, QPainter &painter, primitives &prim) const;
		void parseText   (const QDomElement &dom, QPainter &painter, primitives &prim) const;
		void parsePlcTable(const QDomElement &dom, const QDomElement &plc_data, QPainter &painter) const;
		void setPainterStyle(const QDomElement &dom, QPainter &painter) const;
		
		QHash<QUuid, QPicture> m_pictures_H;
		QHash<QUuid, QPicture> m_low_pictures_H;
			/// Drawings whose texts read normally once an element has
			/// mirrored or turned them, by element and readableKey(), see
			/// getReadablePictures()
		QHash<QPair<QUuid, int>, QPicture> m_readable_pictures_H;
		QHash<QPair<QUuid, int>, QPicture> m_readable_low_pictures_H;
			/// What build() undoes on each text, read by parseText()
		QTransform m_build_texts_undo;
			/// See inkChoice(), for the drawing being built
		QColor m_build_ink;
			/// The drawing being built is that of a P&ID symbol (pid_family)
		bool m_build_pid = false;
		QHash<QUuid, QPixmap> m_pixmap_H;
		QHash<QUuid, primitives> m_primitives_H;
			/// The keys of the drawings of a symbol with options chosen
			/// (cacheKey() with choices), by the key of its plain drawing,
			/// for dropCache()
		QMultiHash<QUuid, QUuid> m_option_keys;
		static ElementPictureFactory* m_factory;
};

#endif // ELEMENTPICTUREFACTORY_H
