# Math

```saffron
import "@math" as Math
```

## Constants

| Name | Value |
|------|-------|
| `Math.pi` | 3.14159265358979... |
| `Math.e` | 2.71828182845905... |
| `Math.tau` | 6.28318530717959... (2pi) |
| `Math.PI` | Alias for `pi` |
| `Math.E` | Alias for `e` |

## Basic functions

| Function | Description |
|----------|-------------|
| `Math.abs(x)` | Absolute value |
| `Math.floor(x)` | Round down to integer |
| `Math.ceil(x)` | Round up to integer |
| `Math.round(x)` | Round to nearest integer |
| `Math.min(a, b)` | Minimum of two values |
| `Math.max(a, b)` | Maximum of two values |
| `Math.clamp(x, lo, hi)` | Clamp x to [lo, hi] |
| `Math.sign(x)` | -1, 0, or 1 |

## Powers and roots

| Function | Description |
|----------|-------------|
| `Math.sqrt(x)` | Square root |
| `Math.pow(x, y)` | x raised to y |

## Trigonometry

| Function | Description |
|----------|-------------|
| `Math.sin(x)` | Sine (radians) |
| `Math.cos(x)` | Cosine (radians) |
| `Math.tan(x)` | Tangent (radians) |
| `Math.asin(x)` | Arcsine |
| `Math.acos(x)` | Arccosine |
| `Math.atan(x)` | Arctangent |
| `Math.atan2(y, x)` | Two-argument arctangent |

## Logarithms

| Function | Description |
|----------|-------------|
| `Math.log(x)` | Natural log (ln) |
| `Math.log2(x)` | Base-2 log |
| `Math.log10(x)` | Base-10 log |

## Utilities

| Function | Description |
|----------|-------------|
| `Math.lerp(a, b, t)` | Linear interpolation |
| `Math.deg_to_rad(d)` | Degrees to radians |
| `Math.rad_to_deg(r)` | Radians to degrees |
| `Math.hypot(x, y)` | Hypotenuse length |
| `Math.map_range(v, in_min, in_max, out_min, out_max)` | Remap value between ranges |
| `Math.is_close(a, b, tolerance)` | Approximate equality |
