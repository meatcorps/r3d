// Deliberately changes both the visible mesh and its shadow.

uniform float lift = 0.75;
uniform float stripeWidth = 0.45;

void vertex()
{
    POSITION.x += 0.7;
    POSITION.y += lift;
}

void fragment()
{
    // Must produce matching holes in the object and shadow.
    if (fract(POSITION.x * 4.0) < stripeWidth)
    discard;
}