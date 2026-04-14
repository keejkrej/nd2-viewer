#include "core/documentreaderfactory.h"

#include "core/czireader.h"
#include "core/nd2reader.h"
#include "core/policydocumentreader.h"

#include <QFileInfo>

std::unique_ptr<DocumentReader> createDocumentReaderForPath(const QString &path, QString *errorMessage)
{
    return createDocumentReaderForPath(path, {}, errorMessage);
}

std::unique_ptr<DocumentReader> createDocumentReaderForPath(const QString &path,
                                                            const DocumentReaderOptions &options,
                                                            QString *errorMessage)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    std::unique_ptr<DocumentReader> reader;
    if (suffix == QStringLiteral("nd2")) {
        reader = std::make_unique<Nd2Reader>();
    } else if (suffix == QStringLiteral("czi")) {
        reader = std::make_unique<CziReader>();
    }

    if (reader) {
        if (!options.forcePolicyWrapper && options.failurePolicy == ReadFailurePolicy::Strict && !options.issueLog) {
            return reader;
        }
        return std::make_unique<PolicyDocumentReader>(std::move(reader), options);
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("Unsupported file type '%1'. Only ND2 and CZI files are supported.")
                            .arg(suffix.isEmpty() ? QStringLiteral("(no extension)") : suffix);
    }

    return {};
}
