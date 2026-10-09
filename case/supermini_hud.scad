// =====================================================================
//  Small HUD enclosure — 2.79" TFT-SPI (GMT279-01M, 142x428, 78 x 25.9 mm PCB)
//  Two parts: top shell (window, 45° visor, rounded back) + bottom plate (screwed from below).
//  Material ASA. Units mm.
//
//  Axes: X = along the display (pin header / wires end = -X = left, holes end = +X = right),
//        Y = depth (0 = front face, +Y toward the windshield), Z = up.
//        Z = 0 is the top surface of the bottom plate; the plate is z = -plate_t .. 0.
//
//  PART = "shell" | "plate" | "plate_nofeet" | "feet" | "assembly"
// =====================================================================
PART = "assembly";

/* [Fit — ASA] */
clr_x   = 0.35;    // horizontal clearance per side (was 0.30, +0.05 after the first print)
clr_z   = 0.15;    // vertical clearance per side

/* [Display, from the drawing] */
pcb_l   = 78.0;  pcb_h = 25.9;  pcb_t = 1.2;
// distances from the pin-header end of the PCB (mm): [start, end]
bl_x    = [4.06, 73.49];   bl_h  = 24.38;   // backlight
tft_x   = [4.83, 72.72];   tft_h = 23.08;   // TFT glass (centred in the backlight)
aa_x    = [5.42, 69.56];   aa_h  = 21.28;   // active area, 64.14 long
stack_t = 2.0;             // BL + TFT thickness
tape_t  = 0.4;             // 3M between BL and PCB
pocket_d = 0.3;            // glass pocket depth in the front panel

/* [Window] */
m_left  = 0.30;            // margin around the active area, pin-header end (limited by the glass edge)
m_right = 1.00;            // holes end
m_z     = 0.40;            // top / bottom
bevel   = 1.0;             // 45° taper on the outside -> thin edge next to the glass (0.7 mm)

/* [Slots for the PCB ends] */
slot_right = 3.5;          // holes end: slot depth
corner     = 2.0;          // pin end: 2 x 2 mm corner pockets (bottom: plate, top: shell)
groove_d   = 0.3;          // groove under the PCB bottom edge in the plate
slot_clr   = 0.15;         // clearance on each side of the PCB thickness

/* [Shell] */
t_f       = 2.0;           // front panel
wall_top  = 2.0;
wall_side = 2.6;
plate_t   = 2.5;
round_r   = 1.5;           // outer edge radius (eco-leather friendly)
y_corner  = 20;            // virtual corner roof/slope: the roof is flat only just past the screen
fillet_r  = 30;            // smooth transition flat top -> slope
yc_bot    = 16;            // plan-view rounding starts here (floor level)
yc_top    = 5;             //   ... and here (top level)
cheek_t   = 3.0;  visor_t = 3.0;   // = 2*round_r: cheeks and visor are rounded on all edges

/* [Screws from below] */
j_pilot = 1.7;  j_hole = 2.3;  j_head = 3.9;

/* [Rear connector: mini-USB breakout, board trimmed] */
mu_w = 20.0;  mu_d = 13.0;  mu_hole_dx = 14.4;  mu_hole_to_face = 10.5;
mu_rec_w = 7.7;  mu_rec_h = 4.0;  mu_out = 1.0;  plug_over = 2.0;
mu_board_t = 1.6;  podium_h = 2.5;  mu_pilot = 2.1;

/* [Feet] */
foot_d = 5;  foot_front = 3;  foot_rear = 2;  pad_depth = 0.6;

$fn = 40;

// ---------------- derived ----------------
S       = 80.2;                       // distance between the pin-end stop wall and the holes-end slot floor
Hc_in   = S/2;                        // inner half width: the right slot floor is the inner side wall itself
W       = 2*(Hc_in + wall_side);
Xl      = -Hc_in;
XRf     = Xl + S;                     // right slot floor
pcb_x0  = Xl + clr_x;                 // PCB pin-end face (final position, pushed to the stop)
z_pb    = -groove_d;                  // PCB bottom edge
z_pt    = z_pb + pcb_h;               // PCB top edge
zc      = (z_pb + z_pt)/2;
ceil_in = z_pt + clr_z;
H       = ceil_in + wall_top;         // outer top
Zb      = -plate_t;                   // outer bottom
visor_len = H - Zb - 2*round_r;       // 45° cheeks
y_tft   = t_f - pocket_d;             // glass front
y_pf    = y_tft + stack_t + tape_t;   // PCB front face
y_pb    = y_pf + pcb_t;               // PCB back face
y_s1    = y_pf - slot_clr;            // slot front wall
y_s2    = y_pb + slot_clr;            // slot back wall
D       = W - visor_len;              // body depth at the floor -> total depth incl. visor = width
function dx(d) = pcb_x0 + d;

// ---------------- back shape (plan dome + side profile with fillet) ----------------
a_half  = W/2;
function cl(z) = max(0, min(H, z));
function yc(z) = yc_bot + (yc_top - yc_bot)*cl(z)/H;
th_deg  = atan((H-Zb)/(D-y_corner));              // straight slope from the corner down to the floor edge at y = D
y_k     = y_corner;
function prof_y(z, yk, Ht, R) = let(zt = Ht - R*(1-cos(th_deg)), yf = yk - R*tan(th_deg/2))
    z >= zt ? yf + sqrt(max(0, R*R - pow(z-(Ht-R),2))) : yk + (Ht - z)/tan(th_deg);
function yr(z)    = prof_y(cl(z), y_k, H, fillet_r);
function yr_in(z) = prof_y(cl(z), y_k - wall_top*tan(th_deg/2), H - wall_top, fillet_r - wall_top);
function outline(z, inner=false) = let(c=yc(z), a = inner ? a_half-wall_side : a_half,
                                       b = max((inner ? yr_in(z) : yr(z)) - c, 0.5))
    concat([[-a,0],[a,0]], [ for (t=[0:4:180]) [a*cos(t), c + b*sin(t)] ]);
function y_in_at(x, z) = let(c=yc(z), a=a_half-wall_side, b=yr_in(z)-c) c + b*sqrt(max(0,1-pow(x/a,2)));
module layers(off, za, zb, n=24, inner=false) {
    for (i=[0:n]) let(t=i/n, z = za + (zb-za)*(1-(1-t)*(1-t)))
        translate([0,0,z]) linear_extrude(0.01) offset(delta=off) polygon(outline(z, inner));
}
module core_outer() { hull() layers(0, Zb, H); }
module core_safe()  { hull() layers(-1.0, Zb+1, H-1, 14); }

// ---------------- mini-USB geometry ----------------
rec_z   = podium_h + mu_board_t + mu_rec_h/2;          // receptacle centre height
mu_y_in = yr(rec_z) - 8.5;                             // board edge / bulkhead start
mu_hy   = mu_y_in + mu_out - mu_hole_to_face;          // mounting holes line

// ---------------- screw pillars ----------------
function pil(sx) = [ [sx*(Hc_in-3.0), 15], [sx*29, y_in_at(29, 6) - 3.3] ];
pillars = [ for (sx=[-1,1]) each pil(sx) ];

// ---------------- shell ----------------
module body_solid() {
    r = round_r;  xo = W/2 - r;
    minkowski() { hull() layers(-r, Zb+r, H-r, 20); sphere(r=r, $fn=16); }
    // 45 deg cheeks (rounded plates) and the visor slab (rounded)
    for (sx=[-1,1]) hull() for (p=[[-visor_len+r, H-r],[r, H-r],[r, Zb+r]])
        translate([sx*xo, p[0], p[1]]) sphere(r=r-0.003, $fn=16);
    hull() for (sx=[-1,1], y=[-visor_len+r, r]) translate([sx*xo, y, H-r]) sphere(r=r-0.003, $fn=16);
}
module cavity() {
    intersection() {
        hull() layers(0, Zb-1, ceil_in, 26, true);
        translate([-Hc_in, t_f, Zb-2]) cube([2*Hc_in, 200, ceil_in-Zb+2]);
    }
}
module shell_cuts() {
    // glass pocket (inner side of the front panel)
    translate([dx(tft_x[0])-clr_x, y_tft, zc-tft_h/2-clr_z])
        cube([tft_x[1]-tft_x[0]+2*clr_x, pocket_d+0.02, tft_h+2*clr_z]);
    // window: narrow edge next to the glass, 45° taper opening to the outside
    wx0 = dx(aa_x[0]) - m_left;   wx1 = dx(aa_x[1]) + m_right;
    wz0 = zc - aa_h/2 - m_z;      wz1 = zc + aa_h/2 + m_z;
    hull() {
        translate([wx0-bevel, -0.02, wz0-bevel]) cube([wx1-wx0+2*bevel, 0.01, wz1-wz0+2*bevel]);
        translate([wx0, bevel, wz0]) cube([wx1-wx0, 0.01, wz1-wz0]);
    }
    translate([wx0, bevel-0.01, wz0]) cube([wx1-wx0, t_f, wz1-wz0]);
    // mini-USB tunnel (plug overmold is plug_over bigger than the receptacle on each side)
    ow = mu_rec_w/2 + plug_over + 0.4;  oh = mu_rec_h/2 + plug_over + 0.4;
    translate([-ow, mu_y_in-0.01, rec_z-oh]) cube([2*ow, 40, 2*oh]);
    // screw pilots from below
    for (p=pillars) translate([p[0], p[1], -0.01]) cylinder(d=j_pilot, h=9);
}
module shell_adds() {
    intersection() {
        core_safe();
        union() {
            // screw pillars, floor to ceiling
            for (p=pillars) translate([p[0], p[1], -0.01]) cylinder(d=6.4, h=ceil_in+0.02);
            // top corner ribs (pin end): straddle the PCB top edge, 2 mm deep
            for (ys=[[t_f-0.01, y_s1], [y_s2, y_s2+1.75]])
                translate([-Hc_in, ys[0], z_pt-corner]) cube([corner, ys[1]-ys[0], ceil_in-(z_pt-corner)+0.02]);
            // right (holes end): short top corner posts, 3.5 mm deep, 2 mm high (like on the left)
            for (ys=[[t_f-0.01, y_s1], [y_s2, y_s2+1.75]])
                translate([XRf-slot_right, ys[0], z_pt-corner]) cube([slot_right+0.6, ys[1]-ys[0], ceil_in-(z_pt-corner)+0.02]);
            // mini-USB bulkhead
            translate([-mu_w/2-1, mu_y_in, -0.01]) cube([mu_w+2, 30, 14]);
        }
    }
}
module shell() {
    difference() {
        union() {
            difference() { body_solid(); cavity(); }
            shell_adds();
        }
        shell_cuts();
    }
}

// ---------------- bottom plate ----------------
foot_pos = [ for (sx=[-1,1]) each [[sx*34, 9, foot_front], [sx*34, 28, foot_rear]] ];
module plate_body() {
    br = 0.7;   // rounded bottom edge
    sh = intersection_shape();
    hull() {
        for (k=[0:4]) let(a=k*22.5, d=br*(1-sin(a)), z=Zb+br*(1-cos(a)))   // quarter-round at the bottom
            translate([0,0,z]) linear_extrude(0.01) offset(delta=-d) sh2d();
        translate([0,0,Zb+br]) linear_extrude(plate_t-br) sh2d();
    }
}
module sh2d() {
    intersection() {
        offset(delta=-0.25) polygon(outline(0, true));
        translate([-Hc_in, t_f+0.25]) square([2*Hc_in, 200]);
    }
}
function intersection_shape() = 0;
module plate_features() {
    // bottom corner ribs (pin end): straddle the PCB bottom edge
    for (ys=[[t_f+0.25, y_s1], [y_s2, y_s2+1.75]])
        translate([-Hc_in+0.25, ys[0], -0.01]) cube([corner-0.25, ys[1]-ys[0], z_pb+corner+0.01]);
    // right (holes end): low corner posts like on the left; the 3.5 mm slot itself is in the shell
    for (ys=[[t_f+0.25, y_s1], [y_s2, y_s2+1.75]])
        translate([Hc_in-slot_right, ys[0], -0.01]) cube([slot_right-0.25, ys[1]-ys[0], z_pb+corner+0.01]);
    // mini-USB podium
    difference() {
        translate([-mu_w/2-1, mu_y_in-mu_d-1, -0.01]) cube([mu_w+2, mu_d+1, podium_h+0.01]);
        translate([-7, mu_y_in-mu_d-2, podium_h-1.5]) cube([14, (mu_hy-1.5)-(mu_y_in-mu_d-2), 2]);
    }
}
module plate_cuts() {
    // groove for the PCB bottom edge
    translate([Xl+0.2, y_s1, z_pb]) cube([XRf-(Xl+0.2), y_s2-y_s1, groove_d+0.02]);
    // mini-USB board screws (M2.5 self-tapping)
    for (s=[-1,1]) translate([s*mu_hole_dx/2, mu_hy, Zb+1]) cylinder(d=mu_pilot, h=podium_h-Zb-1+0.01);
    // case screws, countersunk from below
    for (p=pillars) {
        translate([p[0], p[1], Zb-1]) cylinder(d=j_hole, h=plate_t+2);
        translate([p[0], p[1], Zb-0.01]) cylinder(d1=j_head, d2=j_hole, h=(j_head-j_hole)/2+0.01);
    }
}
module feet(only=false) {
    for (f=foot_pos) translate([f[0], f[1], Zb-f[2]]) difference() {
        cylinder(d=foot_d, h=f[2]+0.6);
        translate([0,0,-0.01]) cylinder(d=foot_d-1.4, h=pad_depth);
    }
}
module plate(with_feet=true) {
    difference() {
        union() { plate_body(); plate_features(); if (with_feet) feet(); }
        plate_cuts();
    }
}

// ---------------- output ----------------
if (PART=="shell")        translate([0,0,H]) rotate([180,0,0]) shell();      // top on the bed
if (PART=="plate")        translate([0,0,0]) plate(true);                    // rests on the feet (use supports)
if (PART=="plate_nofeet") translate([0,0,-Zb]) plate(false);                 // flat on the bed
if (PART=="feet")         for (i=[0:len(foot_pos)-1]) translate([(i%4)*8, floor(i/4)*8, 0])
                              difference() { cylinder(d=foot_d, h=foot_pos[i][2]); translate([0,0,-0.01]) cylinder(d=foot_d-1.4, h=pad_depth); }
if (PART=="assembly")   { color("DimGray") shell(); color("SlateGray") plate(true); }
if (PART=="shell_raw")  shell();
if (PART=="plate_raw")  plate(true);
