#!/usr/bin/env node
'use strict';

const fs = require('fs');
const path = require('path');
const {
    Algorithm,
    hash: argon2Hash,
    verify: argon2Verify,
} = require('@node-rs/argon2');
const {
    normalizeEmail,
    validatePassword,
} = require('../src/email-auth');
const { UserAccountStore, normalizeRole } = require('../src/user-account-store');

function parseArguments(argv) {
    const values = {};
    for (let index = 0; index < argv.length; index += 1) {
        const name = argv[index];
        if (name === '--confirm-direct-activation') {
            values.confirmed = true;
            continue;
        }
        if (!['--database', '--email', '--role'].includes(name) ||
            index + 1 >= argv.length) {
            throw new Error(
                'usage: account-create.js --database <path> --email <email> ' +
                '--role <admin|user> --confirm-direct-activation'
            );
        }
        values[name.slice(2)] = argv[index + 1];
        index += 1;
    }
    if (!values.database || !values.email || !values.role || !values.confirmed) {
        throw new Error(
            'database, email, role and --confirm-direct-activation are required'
        );
    }
    return values;
}

function readPipedPassword() {
    const value = fs.readFileSync(0, 'utf8').replace(/\r?\n$/, '');
    if (/[\r\n]/.test(value)) {
        throw new Error('password input must contain exactly one line');
    }
    return value;
}

function readHiddenPassword() {
    if (!process.stdin.isTTY || typeof process.stdin.setRawMode !== 'function') {
        return Promise.resolve(readPipedPassword());
    }
    process.stdout.write('Password: ');
    process.stdin.setRawMode(true);
    process.stdin.resume();
    return new Promise((resolve, reject) => {
        let value = '';
        const finish = (error) => {
            process.stdin.off('data', onData);
            process.stdin.setRawMode(false);
            process.stdin.pause();
            process.stdout.write('\n');
            if (error) reject(error);
            else resolve(value);
        };
        const onData = chunk => {
            for (const byte of chunk) {
                if (byte === 3) {
                    finish(new Error('cancelled'));
                    return;
                }
                if (byte === 13 || byte === 10) {
                    finish();
                    return;
                }
                if (byte === 8 || byte === 127) {
                    value = value.slice(0, -1);
                } else if (byte >= 32 && byte <= 126) {
                    value += String.fromCharCode(byte);
                }
            }
        };
        process.stdin.on('data', onData);
    });
}

async function main(argv) {
    const options = parseArguments(argv);
    const databasePath = path.resolve(options.database);
    const email = normalizeEmail(options.email);
    const role = normalizeRole(options.role);
    let password = validatePassword(await readHiddenPassword());
    const passwordHash = await argon2Hash(password, {
        algorithm: Algorithm.Argon2id,
        memoryCost: 19456,
        timeCost: 2,
        parallelism: 1,
        outputLen: 32,
    });
    const passwordVerified = await argon2Verify(passwordHash, password);
    password = null;
    const store = new UserAccountStore({ databasePath });
    try {
        const result = store.createVerifiedEmailAccount({
            email,
            passwordHash,
            displayName: email.slice(0, email.indexOf('@')) || 'User',
            role,
            actorId: 'offline-direct-account-create-cli',
        });
        if (result.status === 'exists') {
            throw new Error('the email account already exists; password was not changed');
        }
        process.stdout.write(`${JSON.stringify({
            databasePath,
            status: result.status,
            uid: result.user.uid,
            email: result.user.email,
            displayName: result.user.displayName,
            role: result.user.role,
            verifiedAt: result.user.verifiedAt,
            passwordVerified,
        })}\n`);
    } finally {
        store.close();
    }
}

main(process.argv.slice(2)).catch(error => {
    process.stderr.write(`${error.message}\n`);
    process.exitCode = 2;
});
