export class GamePadColor {
    private r: number;
    private g: number;
    private b: number;
    private a: number;

    constructor(r: number = 0, g: number = 0, b: number = 0, a: number = 1) {
        this.r = this.clamp(r, 0, 255);
        this.g = this.clamp(g, 0, 255);
        this.b = this.clamp(b, 0, 255);
        this.a = this.clamp(a, 0, 1);
    }

    private clamp(value: number, min: number, max: number): number {
        return Math.min(Math.max(value, min), max);
    }

    // 获取各个通道的值
    getChannelValue(channel: 'red' | 'green' | 'blue' | 'alpha'): number {
        switch (channel) {
            case 'red': return this.r;
            case 'green': return this.g;
            case 'blue': return this.b;
            case 'alpha': return this.a;
        }
    }

    // 设置各个通道的值
    setChannelValue(channel: 'red' | 'green' | 'blue' | 'alpha', value: number): void {
        switch (channel) {
            case 'red': this.r = this.clamp(value, 0, 255); break;
            case 'green': this.g = this.clamp(value, 0, 255); break;
            case 'blue': this.b = this.clamp(value, 0, 255); break;
            case 'alpha': this.a = this.clamp(value, 0, 1); break;
        }
    }

    // 设置颜色
    setValue(value: GamePadColor): void {
        this.r = value.getChannelValue('red');
        this.g = value.getChannelValue('green');
        this.b = value.getChannelValue('blue');
        this.a = value.getChannelValue('alpha');
    }

    // 转换为 CSS 字符串
    toString(format: 'hex' | 'rgb' | 'rgba' | 'css' = 'rgba'): string {
        switch (format) {
            case 'hex':
                const r = this.r.toString(16).padStart(2, '0');
                const g = this.g.toString(16).padStart(2, '0');
                const b = this.b.toString(16).padStart(2, '0');
                const a = Math.round(this.a * 255).toString(16).padStart(2, '0');
                return `#${r}${g}${b}${this.a < 1 ? a : ''}`;
            case 'rgb':
                return `rgb(${this.r}, ${this.g}, ${this.b})`;
            case 'rgba':
                return `rgba(${this.r}, ${this.g}, ${this.b}, ${this.a})`;
            case 'css':
                return this.a < 1 ? this.toString('rgba') : this.toString('rgb');
            default:
                return this.toString('rgba');
        }
    }

    // 从 CSS 字符串解析颜色
    static fromString(color: string): GamePadColor {
        // 移除所有空格
        color = color.replace(/\s/g, '');

        // 处理 hex 格式
        if (color.startsWith('#')) {
            const hex = color.slice(1);
            switch (hex.length) {
                case 3: // #RGB
                    return new GamePadColor(
                        parseInt(hex[0] + hex[0], 16),
                        parseInt(hex[1] + hex[1], 16),
                        parseInt(hex[2] + hex[2], 16)
                    );
                case 4: // #RGBA
                    return new GamePadColor(
                        parseInt(hex[0] + hex[0], 16),
                        parseInt(hex[1] + hex[1], 16),
                        parseInt(hex[2] + hex[2], 16),
                        parseInt(hex[3] + hex[3], 16) / 255
                    );
                case 6: // #RRGGBB
                    return new GamePadColor(
                        parseInt(hex.slice(0, 2), 16),
                        parseInt(hex.slice(2, 4), 16),
                        parseInt(hex.slice(4, 6), 16)
                    );
                case 8: // #RRGGBBAA
                    return new GamePadColor(
                        parseInt(hex.slice(0, 2), 16),
                        parseInt(hex.slice(2, 4), 16),
                        parseInt(hex.slice(4, 6), 16),
                        parseInt(hex.slice(6, 8), 16) / 255
                    );
            }
        }

        // 处理 rgb/rgba 格式
        const rgbaMatch = color.match(/^rgba?\((\d+),(\d+),(\d+)(?:,([0-9.]+))?\)$/);
        if (rgbaMatch) {
            return new GamePadColor(
                parseInt(rgbaMatch[1]),
                parseInt(rgbaMatch[2]),
                parseInt(rgbaMatch[3]),
                rgbaMatch[4] ? parseFloat(rgbaMatch[4]) : 1
            );
        }

        // 默认返回黑色
        return new GamePadColor();
    }

    // 克隆颜色
    clone(): GamePadColor {
        return new GamePadColor(this.r, this.g, this.b, this.a);
    }

    // 设置透明度
    setAlpha(alpha: number): GamePadColor {
        this.a = this.clamp(alpha, 0, 1);
        return this;
    }

    // 判断是否相等
    equals(other: GamePadColor): boolean {
        return this.r === other.r &&
               this.g === other.g &&
               this.b === other.b &&
               this.a === other.a;
    }
}
