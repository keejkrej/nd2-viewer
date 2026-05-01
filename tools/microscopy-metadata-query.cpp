/*
 * Raw metadata from vendor SDKs only (no merging, no JSON/XML synthesis beyond what the SDK returns).
 *
 * ND2  — Nikon Nd2ReadSdk (UTF-8 paths via Lim_FileOpenForReadUtf8).
 * CZI  — libCZI (UTF-8 paths via StreamsFactory::CreateDefaultStreamForFile).
 *
 * Usage:
 *   microscopy-metadata-query <file.nd2|czi> [command [args...]]
 *
 * If [command] is omitted:
 *   .nd2 — attributes, experiment, global metadata, textinfo (each Lim_* JSON), plus sequence/coord summary lines.
 *   .czi — document metadata segment XML (ICziMetadata::GetXml) plus subBlockCount from GetStatistics().
 *
 * ND2 commands:  attributes | experiment | metadata | textinfo | frame <seqIndex>
 * CZI commands:   document | subblock <subBlockIndex>
 */

#include <Nd2ReadSdk.h>
#include <libCZI.h>

#include <cctype>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>

namespace
{
void toLowerAscii(std::string &s)
{
    for (char &c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
}

enum class Format { Unknown, Nd2, Czi };

Format detectFormat(const char *path)
{
    const char *slash = std::strrchr(path, '/');
#ifdef _WIN32
    const char *back = std::strrchr(path, '\\');
    if (back && (!slash || back > slash)) {
        slash = back;
    }
#endif
    const char *base = slash ? slash + 1 : path;
    const char *dot = std::strrchr(base, '.');
    if (!dot) {
        return Format::Unknown;
    }
    std::string ext(dot + 1);
    toLowerAscii(ext);
    if (ext == "nd2") {
        return Format::Nd2;
    }
    if (ext == "czi") {
        return Format::Czi;
    }
    return Format::Unknown;
}

void printNd2Block(const char *title, LIMSTR json)
{
    std::cout << "=== " << title << " ===\n";
    if (json) {
        std::cout << json << '\n';
        Lim_FileFreeString(json);
    } else {
        std::cout << "(null)\n";
    }
}

void printUsage(const char *argv0)
{
    std::cerr << "usage: " << argv0 << " <file.nd2|czi> [command [args...]]\n"
              << "\n"
              << "Omit command to dump default raw blocks for the format.\n"
              << "ND2 commands:  attributes | experiment | metadata | textinfo | frame <seqIndex>\n"
              << "CZI commands:   document | subblock <subBlockIndex>\n";
}

unsigned parseUintArg(const char *s, const char *label, bool *ok)
{
    *ok = false;
    char *end = nullptr;
    const unsigned long v = std::strtoul(s, &end, 10);
    if (end == s || *end != '\0' || v > static_cast<unsigned long>(UINT_MAX)) {
        std::cerr << "Invalid " << label << ": \"" << s << "\"\n";
        return 0;
    }
    *ok = true;
    return static_cast<unsigned>(v);
}

int runNd2(int argc, char *argv[])
{
    const LIMFILEHANDLE h = Lim_FileOpenForReadUtf8(argv[1]);
    if (!h) {
        std::cerr << "Failed to open ND2: " << argv[1] << '\n';
        return 1;
    }

    if (argc == 2) {
        printNd2Block("attributes", Lim_FileGetAttributes(h));
        printNd2Block("experiment", Lim_FileGetExperiment(h));
        printNd2Block("metadata", Lim_FileGetMetadata(h));
        printNd2Block("textinfo", Lim_FileGetTextinfo(h));
        const LIMUINT seqCount = Lim_FileGetSeqCount(h);
        std::cout << "=== sequenceCount ===\n" << seqCount << '\n';
        const LIMSIZE coordDim = Lim_FileGetCoordSize(h);
        std::cout << "=== coordDimension ===\n" << coordDim << '\n';
        if (coordDim > 0 && coordDim < 64) {
            char typeBuf[256];
            for (LIMUINT c = 0; c < static_cast<LIMUINT>(coordDim); ++c) {
                const LIMUINT loopSize = Lim_FileGetCoordInfo(h, c, typeBuf, sizeof typeBuf);
                std::cout << "=== coord[" << c << "] ===\n";
                std::cout << "  loopSize: " << loopSize << '\n';
                std::cout << "  type: " << typeBuf << '\n';
            }
        }
        Lim_FileClose(h);
        return 0;
    }

    std::string cmd(argv[2]);
    toLowerAscii(cmd);

    if (cmd == "attributes") {
        printNd2Block("attributes", Lim_FileGetAttributes(h));
    } else if (cmd == "experiment") {
        printNd2Block("experiment", Lim_FileGetExperiment(h));
    } else if (cmd == "metadata") {
        printNd2Block("metadata", Lim_FileGetMetadata(h));
    } else if (cmd == "textinfo") {
        printNd2Block("textinfo", Lim_FileGetTextinfo(h));
    } else if (cmd == "frame") {
        if (argc < 4) {
            std::cerr << "frame requires <seqIndex>\n";
            Lim_FileClose(h);
            return 1;
        }
        bool ok = false;
        const unsigned n = parseUintArg(argv[3], "seqIndex", &ok);
        if (!ok) {
            Lim_FileClose(h);
            return 1;
        }
        const LIMUINT count = Lim_FileGetSeqCount(h);
        if (count > 0 && n >= count) {
            std::cerr << "seqIndex " << n << " out of range (sequenceCount=" << count << ")\n";
            Lim_FileClose(h);
            return 1;
        }
        printNd2Block("frame_metadata", Lim_FileGetFrameMetadata(h, static_cast<LIMUINT>(n)));
    } else {
        std::cerr << "Unknown ND2 command: " << argv[2] << '\n';
        Lim_FileClose(h);
        return 1;
    }

    Lim_FileClose(h);
    return 0;
}

int runCzi(int argc, char *argv[])
{
    libCZI::StreamsFactory::Initialize();
    std::shared_ptr<libCZI::IStream> stream;
    try {
        stream = libCZI::StreamsFactory::CreateDefaultStreamForFile(argv[1]);
    } catch (const std::exception &ex) {
        std::cerr << "Failed to open CZI stream: " << ex.what() << '\n';
        return 1;
    }
    if (!stream) {
        std::cerr << "Failed to create stream for: " << argv[1] << '\n';
        return 1;
    }

    std::shared_ptr<libCZI::ICZIReader> reader = libCZI::CreateCZIReader();
    if (!reader) {
        std::cerr << "CreateCZIReader failed\n";
        return 1;
    }

    try {
        reader->Open(stream);
    } catch (const std::exception &ex) {
        std::cerr << "libCZI Open failed: " << ex.what() << '\n';
        return 1;
    }

    auto closeReader = [&]() {
        try {
            reader->Close();
        } catch (...) {
        }
    };

    if (argc == 2) {
        const std::shared_ptr<libCZI::IMetadataSegment> seg = reader->ReadMetadataSegment();
        std::cout << "=== document_metadata_xml ===\n";
        if (!seg) {
            std::cout << "(null segment)\n";
        } else {
            const std::shared_ptr<libCZI::ICziMetadata> meta = seg->CreateMetaFromMetadataSegment();
            if (!meta) {
                std::cout << "(null ICziMetadata)\n";
            } else {
                std::cout << meta->GetXml() << '\n';
            }
        }
        const libCZI::SubBlockStatistics stats = reader->GetStatistics();
        std::cout << "=== subBlockCount ===\n" << stats.subBlockCount << '\n';
        closeReader();
        return 0;
    }

    std::string cmd(argv[2]);
    toLowerAscii(cmd);

    if (cmd == "document") {
        const std::shared_ptr<libCZI::IMetadataSegment> seg = reader->ReadMetadataSegment();
        std::cout << "=== document_metadata_xml ===\n";
        if (!seg) {
            std::cout << "(null segment)\n";
            closeReader();
            return 0;
        }
        const std::shared_ptr<libCZI::ICziMetadata> meta = seg->CreateMetaFromMetadataSegment();
        if (!meta) {
            std::cout << "(null ICziMetadata)\n";
            closeReader();
            return 0;
        }
        std::cout << meta->GetXml() << '\n';
    } else if (cmd == "subblock") {
        if (argc < 4) {
            std::cerr << "subblock requires <subBlockIndex>\n";
            closeReader();
            return 1;
        }
        bool ok = false;
        const unsigned idx = parseUintArg(argv[3], "subBlockIndex", &ok);
        if (!ok) {
            closeReader();
            return 1;
        }
        const int count = reader->GetStatistics().subBlockCount;
        if (count >= 0 && static_cast<int>(idx) >= count) {
            std::cerr << "subBlockIndex " << idx << " out of range (subBlockCount=" << count << ")\n";
            closeReader();
            return 1;
        }
        std::shared_ptr<libCZI::ISubBlock> sb;
        try {
            sb = reader->ReadSubBlock(static_cast<int>(idx));
        } catch (const std::exception &ex) {
            std::cerr << "ReadSubBlock failed: " << ex.what() << '\n';
            closeReader();
            return 1;
        }
        std::cout << "=== subblock_metadata_xml index=" << idx << " ===\n";
        if (!sb) {
            std::cout << "(null subblock)\n";
            closeReader();
            return 0;
        }
        const std::shared_ptr<libCZI::ISubBlockMetadata> sbm = libCZI::CreateSubBlockMetadataFromSubBlock(sb.get());
        if (!sbm) {
            std::cout << "(null ISubBlockMetadata)\n";
            closeReader();
            return 0;
        }
        std::cout << sbm->GetXml() << '\n';
    } else {
        std::cerr << "Unknown CZI command: " << argv[2] << '\n';
        closeReader();
        return 1;
    }

    closeReader();
    return 0;
}
} // namespace

int main(int argc, char *argv[])
{
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    const Format fmt = detectFormat(argv[1]);
    if (fmt == Format::Unknown) {
        std::cerr << "Unsupported or missing file extension (use .nd2 or .czi): " << argv[1] << '\n';
        return 1;
    }

    if (fmt == Format::Nd2) {
        return runNd2(argc, argv);
    }
    return runCzi(argc, argv);
}
