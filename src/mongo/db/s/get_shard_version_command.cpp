/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */


#include "mongo/db/auth/action_set.h"
#include "mongo/db/auth/action_type.h"
#include "mongo/db/auth/authorization_session.h"
#include "mongo/db/auth/privilege.h"
#include "mongo/db/catalog_raii.h"
#include "mongo/db/commands.h"
#include "mongo/db/s/collection_sharding_runtime.h"
#include "mongo/db/s/sharding_state.h"
#include "mongo/s/grid.h"
#include "mongo/util/str.h"

#define MONGO_LOGV2_DEFAULT_COMPONENT ::mongo::logv2::LogComponent::kSharding


namespace mongo {
namespace {

class GetShardVersion : public BasicCommand {
public:
    GetShardVersion() : BasicCommand("getShardVersion") {}

    std::string help() const override {
        return " example: { getShardVersion : 'alleyinsider.foo'  } ";
    }

    bool supportsWriteConcern(const BSONObj& cmd) const override {
        return false;
    }

    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kAlways;
    }

    bool adminOnly() const override {
        return true;
    }

    Status checkAuthForCommand(Client* client,
                               const std::string& dbname,
                               const BSONObj& cmdObj) const override {
        if (!AuthorizationSession::get(client)->isAuthorizedForActionsOnResource(
                ResourcePattern::forExactNamespace(parseNs({boost::none, dbname}, cmdObj)),
                ActionType::getShardVersion)) {
            return Status(ErrorCodes::Unauthorized, "Unauthorized");
        }
        return Status::OK();
    }

    NamespaceString parseNs(const DatabaseName& dbName, const BSONObj& cmdObj) const override {
        return NamespaceString(dbName.tenantId(), CommandHelpers::parseNsFullyQualified(cmdObj));
    }

    bool run(OperationContext* opCtx,
             const DatabaseName& dbName,
             const BSONObj& cmdObj,
             BSONObjBuilder& result) override {
        const NamespaceString nss(parseNs(dbName, cmdObj));

        uassertStatusOK(ShardingState::get(opCtx)->canAcceptShardedCommands());

        result.append(
            "configServer",
            Grid::get(opCtx)->shardRegistry()->getConfigServerConnectionString().toString());

        AutoGetCollection autoColl(opCtx, nss, MODE_IS, AutoGetCollectionViewMode::kViewsPermitted);
        auto* const csr = CollectionShardingRuntime::get(opCtx, nss);

        const auto optMetadata = csr->getCurrentMetadataIfKnown();
        if (!optMetadata) {
            result.append("global", "UNKNOWN");

            if (cmdObj["fullMetadata"].trueValue()) {
                result.append("metadata", BSONObj());
            }
        } else {
            const auto& metadata = *optMetadata;
            result.appendTimestamp("global", metadata.getShardVersion().toLong());

            if (cmdObj["fullMetadata"].trueValue()) {
                BSONObjBuilder metadataBuilder(result.subobjStart("metadata"));
                if (metadata.isSharded()) {
                    metadataBuilder.appendTimestamp("collVersion",
                                                    metadata.getCollVersion().toLong());
                    metadataBuilder.append("collVersionEpoch", metadata.getCollVersion().epoch());
                    metadataBuilder.append("collVersionTimestamp",
                                           metadata.getCollVersion().getTimestamp());

                    metadataBuilder.appendTimestamp("shardVersion",
                                                    metadata.getShardVersionForLogging().toLong());
                    metadataBuilder.append("shardVersionEpoch",
                                           metadata.getShardVersionForLogging().epoch());
                    metadataBuilder.append("shardVersionTimestamp",
                                           metadata.getShardVersionForLogging().getTimestamp());

                    metadataBuilder.append("keyPattern", metadata.getShardKeyPattern().toBSON());

                    BSONArrayBuilder chunksArr(metadataBuilder.subarrayStart("chunks"));
                    metadata.toBSONChunks(&chunksArr);
                    chunksArr.doneFast();
                }
                metadataBuilder.doneFast();
            }
        }

        return true;
    }

} getShardVersionCmd;

}  // namespace
}  // namespace mongo
