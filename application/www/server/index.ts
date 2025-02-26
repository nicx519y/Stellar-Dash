import express from 'express';
import cors from 'cors';
import { readConfig, writeConfig } from './store';

const app = express();
app.use(cors());
app.use(express.json());

// Profile APIs
app.get('/api/profile-list', async (req, res) => {
    try {
        const config = await readConfig();
        res.json({
            errNo: 0,
            data: {
                profileList: config.profiles,
            },
        });
    } catch (error) {
        res.status(500).json({ errNo: 1, errorMessage: 'Failed to fetch profile list' });
    }
});

// ... 其他 API 路由

app.listen(3001, () => {
    console.log('API server running on port 3001');
}); 