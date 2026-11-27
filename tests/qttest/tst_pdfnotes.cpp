// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "pdf_links.h"
#include "pidcodification.h"
#include "print/projectprintwindow.h"
#include "qetinformation.h"
#include "qetproject.h"

#include <QFile>
#include <QTemporaryDir>

// The notes a PDF export puts on its symbols ("Enter the component details"):
// every line names its information, P&ID ones included, and no viewer draws
// a frame around them.
class tst_pdfnotes : public QObject
{
	Q_OBJECT

	private slots:
			// A key QElectroTech names keeps its name; a field of a P&ID tag
			// and a datum of the codification get theirs; any other key is
			// written as it is, never as nothing
		void everyLineHasAName()
		{
			QETProject project;
			PidCodification codification;
			PidCodification::DataField power;
			power.name = QStringLiteral("power");
			power.classes = {QStringLiteral("pump")};
			power.label = QStringLiteral("Rated power");
			QVERIFY(codification.addDataField(power));
			project.setPidCodification(codification);

			QCOMPARE(ProjectPrintWindow::noteKeyName(&project, QETInformation::ELMT_LABEL),
					 QETInformation::translatedInfoKey(QETInformation::ELMT_LABEL));
			QCOMPARE(ProjectPrintWindow::noteKeyName(&project, QStringLiteral("pid_train")), QStringLiteral("Train"));
			QCOMPARE(ProjectPrintWindow::noteKeyName(&project, QStringLiteral("pid_order")), QStringLiteral("Number"));
			QCOMPARE(ProjectPrintWindow::noteKeyName(&project, QStringLiteral("pid_letters")),
					 QStringLiteral("Letters of identification"));
			QCOMPARE(ProjectPrintWindow::noteKeyName(&project, QStringLiteral("pid_power")), QStringLiteral("Rated power"));
			QCOMPARE(ProjectPrintWindow::noteKeyName(&project, QStringLiteral("my_property")), QStringLiteral("my_property"));
			QCOMPARE(ProjectPrintWindow::noteKeyName(nullptr, QStringLiteral("pid_power")), QStringLiteral("pid_power"));
		}

			// The link Qt writes becomes a square with its contents, an empty
			// appearance and no border
		void theNoteHasNoBorder()
		{
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString path = dir.filePath(QStringLiteral("notes.pdf"));
			QByteArray pdf("%PDF-1.4\n");
			const int object = pdf.size();
			pdf += "1 0 obj\n<<\n/Type /Annot\n/Subtype /Link\n/Rect [0 0 10 10]\n/Border [0 0 0]\n"
				   "/A <<\n/S /URI\n/URI (http://componentinfo.local/0)\n>>\n>>\nendobj\n";
			const int xref = pdf.size();
			pdf += "xref\n0 2\n0000000000 65535 f \n"
				   + QByteArray::number(object).rightJustified(10, '0') + " 00000 n \n"
				   "trailer\n<<\n/Size 2\n>>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
			{
				QFile file(path);
				QVERIFY(file.open(QIODevice::WriteOnly));
				file.write(pdf);
			}
			PdfLinks::convertComponentInfoAnnotations(path, {PdfLinks::ComponentInfo{QStringLiteral("Label: U03 PC 001")}});
			QFile file(path);
			QVERIFY(file.open(QIODevice::ReadOnly));
			const QByteArray converted = file.readAll();
			QVERIFY2(converted.contains("/Subtype /Square"), converted.constData());
			QVERIFY(converted.contains("/Contents <"));
			QVERIFY(converted.contains("/BS << /W 0 >>"));
			QVERIFY(!converted.contains("componentinfo.local"));
		}
};

QET_APP_TEST_MAIN(tst_pdfnotes)
#include "tst_pdfnotes.moc"
