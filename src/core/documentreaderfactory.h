#pragma once

#include <memory>

class DocumentReader;
class QString;

[[nodiscard]] std::unique_ptr<DocumentReader> createDocumentReaderForPath(const QString &path, QString *errorMessage = nullptr);
