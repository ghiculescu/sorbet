#include "main/lsp/ErrorFlusherLSP.h"
#include "common/FileSystem.h"
#include "core/lsp/QueryResponse.h"

using namespace std;
namespace sorbet::realmain::lsp {
ErrorFlusherLSP::ErrorFlusherLSP(const u4 epoch, shared_ptr<ErrorReporter> errorReporter)
    : epoch(epoch), errorReporter(errorReporter){};

void ErrorFlusherLSP::flushErrors(spdlog::logger &logger, vector<unique_ptr<core::ErrorQueueMessage>> errors,
                                  const core::GlobalState &gs) {
    UnorderedMap<core::FileRef, vector<std::unique_ptr<core::Error>>> errorsAccumulated;
    vector<core::FileRef> filesTypechecked;

    for (auto &error : errors) {
        if (error->kind == core::ErrorQueueMessage::Kind::Error) {
            if (error->error->isSilenced) {
                continue;
            }

            prodHistogramAdd("error", error->error->what.code, 1);
            auto file = error->whatFile;
            filesTypechecked.emplace_back(file);
            errorsAccumulated[file].emplace_back(std::move(error->error));
        }
    }

    vector<unique_ptr<core::Error>> emptyErrorList;
    for (auto &file : filesTypechecked) {
        auto it = errorsAccumulated.find(file);
        vector<unique_ptr<core::Error>> &errors = it == errorsAccumulated.end() ? emptyErrorList : it->second;
        errorReporter->pushDiagnostics(epoch, file, errors, gs);
    }
}

void ErrorFlusherLSP::flushErrorCount(spdlog::logger &logger, int count) {}

void ErrorFlusherLSP::flushAutocorrects(const core::GlobalState &gs, FileSystem &fs) {
    UnorderedMap<core::FileRef, string> sources;

    for (auto &autocorrect : autocorrects) {
        for (auto &edit : autocorrect.edits) {
            auto file = edit.loc.file();
            if (!sources.count(file)) {
                sources[file] = fs.readFile(file.data(gs).path());
            }
        }
    }

    auto toWrite = core::AutocorrectSuggestion::apply(autocorrects, sources);
    for (auto &entry : toWrite) {
        fs.writeFile(entry.first.data(gs).path(), entry.second);
    }
    autocorrects.clear();
}

} // namespace sorbet::realmain::lsp
