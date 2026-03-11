-- Price Fraction (Rosetta Code)
-- Auto-map the pricing function over an array of prices.

fn priceFraction(price Float) Float {
    if (price < 0.06) { 0.10 }
    else if (price < 0.11) { 0.18 }
    else if (price < 0.16) { 0.26 }
    else if (price < 0.21) { 0.32 }
    else if (price < 0.26) { 0.38 }
    else if (price < 0.31) { 0.44 }
    else if (price < 0.36) { 0.50 }
    else if (price < 0.41) { 0.54 }
    else if (price < 0.46) { 0.58 }
    else if (price < 0.51) { 0.62 }
    else if (price < 0.56) { 0.66 }
    else if (price < 0.61) { 0.70 }
    else if (price < 0.66) { 0.74 }
    else if (price < 0.71) { 0.78 }
    else if (price < 0.76) { 0.82 }
    else if (price < 0.81) { 0.86 }
    else if (price < 0.86) { 0.90 }
    else if (price < 0.91) { 0.94 }
    else if (price < 0.96) { 0.98 }
    else { 1.00 }
}

-- Auto-map priceFraction over test prices
[0.04, 0.10, 0.15, 0.20, 0.25, 0.30, 0.35, 0.50, 0.70, 0.95] priceFraction println;
