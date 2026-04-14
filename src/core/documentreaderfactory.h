#pragma once

#include "core/readfailurepolicy.h"

#include <memory>

class DocumentReader;
class QString;

[[nodiscard]] std::unique_ptr<DocumentReader> createDocumentReaderForPath(const QString &path, QString *errorMessage = nullptr);
[[nodiscard]] std::unique_ptr<DocumentReader> createDocumentReaderForPath(const QString &path,
                                                                          const DocumentReaderOptions &options,
                                                                          QString *errorMessage = nullptr);
