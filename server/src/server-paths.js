'use strict';

const path = require('path');

function resolveConfiguredDirectory(
    environment,
    variableName,
    fallback,
    production
) {
    const configured = String(environment[variableName] || '').trim();
    if (production && !configured) {
        throw new Error(`${variableName} is required in production`);
    }
    if (production && !path.isAbsolute(configured)) {
        throw new Error(`${variableName} must be an absolute path in production`);
    }
    return path.resolve(configured || fallback);
}

function resolveServerStoragePaths(
    environment = process.env,
    serverRoot = path.join(__dirname, '..')
) {
    const production = environment.NODE_ENV === 'production';
    const dataDir = resolveConfiguredDirectory(
        environment,
        'HBOX_SERVER_DATA_DIR',
        path.join(serverRoot, 'data'),
        production
    );
    const uploadDir = resolveConfiguredDirectory(
        environment,
        'HBOX_SERVER_UPLOAD_DIR',
        path.join(serverRoot, 'uploads'),
        production
    );
    return {
        dataDir,
        uploadDir,
        firmwareDataFile: path.join(dataDir, 'firmware_list.json'),
        deviceDataFile: path.join(dataDir, 'device_ids.json'),
        accountDatabase: path.join(dataDir, 'accounts.sqlite3'),
        userAccountDatabase: path.join(dataDir, 'user_accounts.sqlite3'),
        switchMappingDatabase: path.join(dataDir, 'switch_mappings.sqlite3')
    };
}

module.exports = {
    resolveServerStoragePaths
};
