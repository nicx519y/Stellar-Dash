#!/usr/bin/env node
'use strict';

const path = require('path');
const { normalizeEmail } = require('../src/email-auth');
const { UserAccountStore, normalizeRole } = require('../src/user-account-store');

function parseArguments(argv) {
    const values = {};
    for (let index = 0; index < argv.length; index += 1) {
        const name = argv[index];
        if (!['--database', '--email', '--role'].includes(name) ||
            index + 1 >= argv.length) {
            throw new Error(
                'usage: account-role.js --database <path> --email <email> --role <admin|user>'
            );
        }
        values[name.slice(2)] = argv[index + 1];
        index += 1;
    }
    if (!values.database || !values.email || !values.role) {
        throw new Error('database, email and role are required');
    }
    return values;
}

function main(argv) {
    const options = parseArguments(argv);
    const databasePath = path.resolve(options.database);
    const email = normalizeEmail(options.email);
    const role = normalizeRole(options.role);
    const store = new UserAccountStore({ databasePath });
    try {
        const result = store.grantRoleByEmail({
            email,
            role,
            actorId: 'offline-account-role-cli',
        });
        process.stdout.write(`${JSON.stringify({ databasePath, ...result })}\n`);
    } finally {
        store.close();
    }
}

try {
    main(process.argv.slice(2));
} catch (error) {
    process.stderr.write(`${error.message}\n`);
    process.exitCode = 2;
}
