# The tyre model of Tyre Mod Edition
## Part 1: some basics about tyres
Tyres are rubber parts that the kart uses to transmit force from the engine to the track. They transmit this force by gripping the track: there needs to be friction, the best tyres are those that have high friction and refuse to easily slide against the track.

In STK, karts produce their engine force on the rear wheels, which are used for driving the kart, and use the front wheels for turning.

It is useful to make a distinction between the grip of the rear wheels, which will only be applied in the same direction the kart is travelling and dictates how much and how easily it accelerates ("forward grip"?), and the grip of the front wheels, which will go slightly against the general direction of the kart and so is a bit different ("lateral grip").

Tyres are usually made of rubber, a material that when hot becomes sticky and a bit flexible, making it perfect for racing tyres. However, since rubber is polymers (a very unstable kind of chemical compount), there are infinitely many different types, and when subjected to stress and temperature these types mutate.

When the tyre is put under a lot of stress, it starts to heat up (heating will not be mentioned after this for simplicity) and importantly it starts to lose material, like how sandpaper can be used to make wood smaller. This makes the tyre different from how the manufacturer designed it, gives it a more uneven surface area, and overall makes it harder for it to grip the track. The laptimes are slower when it can grip the track worse.

The way tyres are subject to stress is by force. Force has a very convenient formula: `Force = mass x acceleration`.

So, we need to know how much the tyre is accelerating and how much mass is being put on top of it. The mass is just the kart weight.

For the rear tyres, to calculate the acceleration in a period of time we take the linear acceleration, `a = (v2 - v1)/(t2 - t1)`, where `t1` and `t2` are two time instants of the beggining and start of the period, and `v1`, `v2` the speeds they had in those instants.

For the front tyres, there is something called centripetal acceleration, which tells you the rotational acceleration the kart is subjected to based on its turn radius `r` (proportional to how wide the turn angle is) and its linear speed `v`: `Ca = v²/r`

So we have: `force_rear = mass*a`; `force_front = mass*Ca`. They are the forces exerted on the rear and front tyres, respectively. They are also the only way our tyres could get degraded.

Note: this if false, there is a minor factor: just like how rolling an eraser on a piece of paper will take some effort even at constant speed, in real life there is friction, which means just by rolling, even if the acceleration is 0, there will be some force because of rolling resistance requiring it to keep speed constant: `force_rolling = m*k*speed`, where `k` is some very small constant that indicates how much difficulty the tyres have for rolling.

## Part 2: are we going to simulate the individual atoms of the tyres or what!?
No. The way we model tyre degradation is by assigning each tyre, when fresh out of the factory, a "health" status which indicates, directly, how much of its material is left. If a tyre at full health weighs 5kg, a tyre at half health weighs 2.5kgs (though we don't take this into account in the calculations, and we also ignore the change in tyre size). Under this model, any force directly removes a specific amount of rubber in units `material/s`, which directly results in degradation.

Then, to model the actual performance change, we can have a continuous response function that says how much performance is lost at 100% health, 90% health, etc. This function can be manually adjusted, giving full control over the tyre's characteristics. The function can then be applied to the kart performance in different ways.

## Part 3: how does TME compute degradation?

In STK, the grip simulation is given by the bullet engine. I experimented long and hard with it, and found it to be no good to touch it. So, I came up with the next best thing: if the rear tyres have less grip, this is equivalent to the engine having less power. If the front tyres have less grip, this is equivalent to the kart turning less. (For gameplay reasons, rear tyre degradation also affects top speed. This is a purely game design decision and in real life top speed doesn't really change with tyre degradation).

The continuous response functions are defined for topspeed and acceleration (given the rear health as input) and for turning (given the front health as input) by linearly interpolated curves in the config file, and they are required to be defined in the interval `[0, 100]`.

First of all, degradation is calculated every physics frame, in 2.X this is: every 1/120 seconds ("a frame"), the degradation computation routine is called and it will calculate what the *last* 1/120 seconds of degradation were. Note this routine has several inputs, and crucially the turning values it receives are pre-application of the response curves to the turning (see part 4). So, even when you degrade turning, if your turning input is the same, it will degrade the same as if you had full health (this is not some kind of gift to the player, in fact it's the opposite: usually as the kart degrades you need to turn *harder* in the same corners. This helps recreate the effect of wheel slip making real life tyres degrade more the more they degrade, but it is not very pronounced either, just like in real life)

Firstly, note "acceleration" can includes deceleration when braking of lifting, which degrades tyres in the same way. Also, tyres do not degrade at all when the kart is in the air.

Importantly, linear acceleration isn't computed by blindly taking the described theoretical formula with the last frame's speed and the current speed. Instead, the speed of the *last 6* (the specific amount could change in the future) physics frames (equivalent to a 6/120 = 1/20s = 50ms interval) is stored, and the current speed is compared with *each of them* (that is, how much did the kart accelerate from frame 0 to 6, 1 to 6, ...,  5 to 6?), and the *minimum*  (in absolute value)  is taken. This is to normalize acceleration as the STK topspeed transition system and similar things like to give sudden and fairly ridiculous speed changes to its karts. Additionally, if acceleration in absolute value surpasses a manually-set threshold, it will be simply treated as 0. This is because, over certain values while the kart is stopping, the tyres would degrade by an amount close to infinity when in real life they simply lose grip and transmit 0 force, it's just STK has ideal tyres that stupidly destroy themselves, so we throw away the bad data this way.

Centripetal acceleration is computer with the usual formila, which means speed affects degradation way more than how tight you are turning.

Each kart has two different healths, one for the two rear tyres and another for the two front tyres. It was considered too complex to allow assymmetrical degradation. The tyres start with a configurable amount of health, usually in the thousands (technically the health unit is force xD). Every frame, the degradation is computer with the rolling force, rear force, and front force. The rolling force is substracted from both healths, and the rear/front forces are substrated from their individual healths.

There are configurable factors such as: how much is rear degradation multiplied by when braking? How much is it multiplied when having offroad slowdown? From what slowdown threshold? How much is front degradation multiplied by when skidding? How much are the tyres damaged if you have a crash/bump with the wall?

## Part 4: how are the response functions applied to the kart?

In the config file, it can be configured how these response functions should be combined with the kart stats: either by multiplying, or by subtracting. Let's taoe multiplying as it's the most common mode. Some initial constants can also be configured.

Each frame, where normally the engine force would be applied, there is a function call that multiplies it by whatever it says in the response function for the current rear tyre health (e.g. x0.8). Same for the turning radius, the turning angle of the wheels is multiplied by whatever the response function with the current front tyre health says.

The topspeed is multiplied all the same as the engine force (but with its own response curve), but note only the *base* topspeed (before any boosts are applied) changes when tyres degrade, so the bonus topspeed of a zipper, nitro or skidding donesn't change. In the case of engine force, it also applies to boosts, but with topspeed this is not the case.
