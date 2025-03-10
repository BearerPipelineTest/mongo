/**
 *    Copyright (C) 2020-present MongoDB, Inc.
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

#pragma once

#include "mongo/base/string_data.h"
#include "mongo/db/repl/tenant_migration_donor_access_blocker.h"
#include "mongo/db/repl/tenant_migration_recipient_access_blocker.h"
#include "mongo/util/string_map.h"

namespace mongo {

class TenantMigrationAccessBlockerRegistry {
    TenantMigrationAccessBlockerRegistry(const TenantMigrationAccessBlockerRegistry&) = delete;
    TenantMigrationAccessBlockerRegistry& operator=(const TenantMigrationAccessBlockerRegistry&) =
        delete;

public:
    struct DonorRecipientAccessBlockerPair {
        DonorRecipientAccessBlockerPair() = default;
        DonorRecipientAccessBlockerPair(
            std::shared_ptr<TenantMigrationDonorAccessBlocker> donor,
            std::shared_ptr<TenantMigrationRecipientAccessBlocker> recipient)
            : _donor(donor), _recipient(recipient) {}

        std::shared_ptr<TenantMigrationAccessBlocker> getAccessBlocker(
            TenantMigrationAccessBlocker::BlockerType type) const {
            if (type == TenantMigrationAccessBlocker::BlockerType::kDonor) {
                return _donor;
            }
            return _recipient;
        }

        void setAccessBlocker(std::shared_ptr<TenantMigrationAccessBlocker> mtab) {
            invariant(mtab);
            if (mtab->getType() == TenantMigrationAccessBlocker::BlockerType::kDonor) {
                invariant(!_donor);
                _donor = mtab;
            } else {
                invariant(!_recipient);
                _recipient = mtab;
            }
        }

        void clearAccessBlocker(TenantMigrationAccessBlocker::BlockerType type) {
            if (type == TenantMigrationAccessBlocker::BlockerType::kDonor) {
                if (!_donor) {
                    return;
                }
                checked_pointer_cast<TenantMigrationDonorAccessBlocker>(_donor)->interrupt();
                _donor.reset();
            } else {
                if (!_recipient) {
                    return;
                }
                _recipient.reset();
            }
        }

    private:
        std::shared_ptr<TenantMigrationAccessBlocker> _donor;
        std::shared_ptr<TenantMigrationAccessBlocker> _recipient;
    };
    TenantMigrationAccessBlockerRegistry() = default;

    static const ServiceContext::Decoration<TenantMigrationAccessBlockerRegistry> get;

    /**
     * Adds an entry for (tenantId, mtab). There must be no blocker of the same type (donor or
     * recipient) for this tenantId already.
     */
    void add(StringData tenantId, std::shared_ptr<TenantMigrationAccessBlocker> mtab);

    /**
     * Adds donor access blocker, throws ConflictingOperationInProgress if one exists. The blocker
     * protocol must be MigrationProtocolEnum::kShardMerge.
     */
    void addShardMergeDonorAccessBlocker(std::shared_ptr<TenantMigrationDonorAccessBlocker> mtab);

    /**
     * Removes the entry for (tenantId, mtab)
     */
    void remove(StringData tenantId, TenantMigrationAccessBlocker::BlockerType type);

    /**
     * Removes the donor access blocker, if any. Assumes migrationId refers to a migration with
     * protocol MigrationProtocolEnum::kShardMerge.
     */
    void removeShardMergeDonorAccessBlocker(const UUID& migrationId);

    /**
     * Remove all access blockers of the provided type for a migration.
     */
    void removeAccessBlockersForMigration(const UUID& migrationId,
                                          TenantMigrationAccessBlocker::BlockerType type);

    /**
     * Removes all mtabs of the given type.
     */
    void removeAll(TenantMigrationAccessBlocker::BlockerType type);

    /**
     * Iterates through each of the TenantMigrationAccessBlockers and
     * returns the first 'DonorRecipientAccessBlockerPair' it finds whose tenantId is a prefix for
     * dbName.
     */
    boost::optional<DonorRecipientAccessBlockerPair> getTenantMigrationAccessBlockerForDbName(
        StringData dbName);

    /**
     * Iterates through each of the TenantMigrationAccessBlockers and
     * returns the first 'TenantMigrationAccessBlocker' it finds whose 'tenantId' is a prefix for
     * 'dbName' and is of the requested type.
     */
    std::shared_ptr<TenantMigrationAccessBlocker> getTenantMigrationAccessBlockerForDbName(
        StringData dbName, TenantMigrationAccessBlocker::BlockerType type);

    /**
     * Searches through TenantMigrationAccessBlockers and
     * returns the TenantMigrationAccessBlocker that matches tenantId.
     */
    std::shared_ptr<TenantMigrationAccessBlocker> getTenantMigrationAccessBlockerForTenantId(
        StringData tenantId, TenantMigrationAccessBlocker::BlockerType type);

    using applyAllCallback = std::function<void(
        std::string tenantId, std::shared_ptr<TenantMigrationAccessBlocker>& mtab)>;
    /**
     * Applies callback to all TenantMigrationAccessBlockers of the desired type.
     */
    void applyAll(TenantMigrationAccessBlocker::BlockerType type, applyAllCallback&& callback);

    /**
     * Shuts down each of the TenantMigrationAccessBlockers and releases the shared_ptrs to the
     * TenantMigrationAccessBlockers from the map.
     */
    void shutDown();

    /**
     * Iterates through each of the TenantMigrationAccessBlockers stored by the mapping
     * and appends the server status of each blocker to the BSONObjBuilder.
     */
    void appendInfoForServerStatus(BSONObjBuilder* builder) const;

    /**
     * Notifies all the TenantMigrationAccessBlockers that the given opTime has been majority
     * committed.
     *
     * This is called while holding a very hot mutex (the ReplicationCoordinator mutex). Therefore
     * it should avoid doing any work that can be done later, and avoid calling back into any
     * replication functions that take this mutex (which would cause self-deadlock).
     */
    void onMajorityCommitPointUpdate(repl::OpTime opTime);

    std::shared_ptr<executor::TaskExecutor> getAsyncBlockingOperationsExecutor();

private:
    boost::optional<DonorRecipientAccessBlockerPair> _getTenantMigrationAccessBlockersForDbName(
        StringData dbName, WithLock);

    bool _hasDonorAccessBlocker(WithLock, StringData dbName);

    void _remove(WithLock, StringData tenantId, TenantMigrationAccessBlocker::BlockerType type);

    std::shared_ptr<TenantMigrationDonorAccessBlocker> _donorAccessBlocker;

    using TenantMigrationAccessBlockersMap = StringMap<DonorRecipientAccessBlockerPair>;

    std::shared_ptr<executor::TaskExecutor> _asyncBlockingOperationsExecutor;

    mutable Mutex _mutex = MONGO_MAKE_LATCH("TenantMigrationAccessBlockerRegistry::_mutex");
    TenantMigrationAccessBlockersMap _tenantMigrationAccessBlockers;
};

}  // namespace mongo
