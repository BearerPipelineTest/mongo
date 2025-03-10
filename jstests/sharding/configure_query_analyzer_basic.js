/**
 * Tests support for the configureQueryAnalyzer command.
 *
 * @tags: [requires_fcv_61, featureFlagAnalyzeShardKey]
 */
(function() {
"use strict";

function runTestExistingNs(conn, ns) {
    jsTest.log(
        `Running configureQueryAnalyzer command against an existing collection ${ns} on ${conn}`);

    // Cannot set 'sampleRate' to 0.
    assert.commandFailedWithCode(
        conn.adminCommand({configureQueryAnalyzer: ns, mode: "full", sampleRate: 0}),
        ErrorCodes.InvalidOptions);

    // Can set 'sampleRate' to > 0.
    assert.commandWorked(
        conn.adminCommand({configureQueryAnalyzer: ns, mode: "full", sampleRate: 0.1}));
    assert.commandWorked(
        conn.adminCommand({configureQueryAnalyzer: ns, mode: "full", sampleRate: 1}));
    assert.commandWorked(
        conn.adminCommand({configureQueryAnalyzer: ns, mode: "full", sampleRate: 1000}));

    // Cannot specify 'sampleRate' when 'mode' is "off".
    assert.commandFailedWithCode(
        conn.adminCommand({configureQueryAnalyzer: ns, mode: "off", sampleRate: 1}),
        ErrorCodes.InvalidOptions);
    assert.commandWorked(conn.adminCommand({configureQueryAnalyzer: ns, mode: "off"}));

    // Cannot specify read/write concern.
    assert.commandFailedWithCode(conn.adminCommand({
        configureQueryAnalyzer: ns,
        mode: "full",
        sampleRate: 1,
        readConcern: {level: "available"}
    }),
                                 ErrorCodes.InvalidOptions);
    assert.commandFailedWithCode(conn.adminCommand({
        configureQueryAnalyzer: ns,
        mode: "full",
        sampleRate: 1,
        writeConcern: {w: "majority"}
    }),
                                 ErrorCodes.InvalidOptions);
}

function runTestNonExistingNs(conn, ns) {
    jsTest.log(`Running configureQueryAnalyzer command against an non-existing collection ${
        ns} on ${conn}`);
    assert.commandFailedWithCode(
        conn.adminCommand({configureQueryAnalyzer: ns, mode: "full", sampleRate: 1}),
        ErrorCodes.NamespaceNotFound);
}

{
    const st = new ShardingTest({shards: 1, rs: {nodes: 2}});
    const shard0Primary = st.rs0.getPrimary();
    const shard0Secondaries = st.rs0.getSecondaries();
    const configPrimary = st.configRS.getPrimary();
    const configSecondaries = st.configRS.getSecondaries();

    const dbName = "testDb";
    const nonExistingNs = dbName + ".nonExistingColl";

    const shardedNs = dbName + ".shardedColl";
    const shardKey = {key: 1};
    assert.commandWorked(st.s.adminCommand({enableSharding: dbName}));
    st.ensurePrimaryShard(dbName, st.shard0.name);
    assert.commandWorked(st.s.adminCommand({shardCollection: shardedNs, key: shardKey}));

    const unshardedCollName = "unshardedColl";
    const unshardedNs = dbName + "." + unshardedCollName;
    assert.commandWorked(st.s.getDB(dbName).createCollection(unshardedCollName));

    // Verify that the command is supported on mongos and configsvr primary mongod.
    function runTestSupported(conn) {
        runTestExistingNs(conn, unshardedNs);
        runTestExistingNs(conn, shardedNs);
        runTestNonExistingNs(conn, nonExistingNs);
    }

    runTestSupported(st.s);
    runTestSupported(configPrimary);

    // Verify that the command is not supported on configsvr secondary mongods or any shardvr
    // mongods.
    function runTestNotSupported(conn, errorCode) {
        assert.commandFailedWithCode(
            conn.adminCommand({configureQueryAnalyzer: unshardedNs, mode: "full", sampleRate: 1}),
            errorCode);
        assert.commandFailedWithCode(
            conn.adminCommand({configureQueryAnalyzer: shardedNs, mode: "full", sampleRate: 1}),
            errorCode);
    }

    configSecondaries.forEach(node => {
        runTestNotSupported(node, ErrorCodes.NotWritablePrimary);
    });
    runTestNotSupported(shard0Primary, ErrorCodes.IllegalOperation);
    shard0Secondaries.forEach(node => {
        runTestNotSupported(node, ErrorCodes.NotWritablePrimary);
    });

    st.stop();
}

{
    const rst = new ReplSetTest({nodes: 2});
    rst.startSet();
    rst.initiate();
    const primary = rst.getPrimary();
    const secondaries = rst.getSecondaries();

    const dbName = "testDb";
    const nonExistingNs = dbName + ".nonExistingColl";

    const unshardedCollName = "unshardedColl";
    const unshardedNs = dbName + "." + unshardedCollName;
    assert.commandWorked(primary.getDB(dbName).createCollection(unshardedCollName));

    // Verify that the command is supported on primary mongod.
    function runTestSupported(conn) {
        runTestExistingNs(conn, unshardedNs);
        runTestNonExistingNs(conn, nonExistingNs);
    }

    runTestSupported(primary, unshardedNs);

    // Verify that the command is not supported on secondary mongods.
    function runTestNotSupported(conn, errorCode) {
        assert.commandFailedWithCode(
            conn.adminCommand({configureQueryAnalyzer: unshardedNs, mode: "full", sampleRate: 1}),
            errorCode);
    }

    secondaries.forEach(node => {
        runTestNotSupported(node, ErrorCodes.NotWritablePrimary);
    });

    rst.stopSet();
}
})();
