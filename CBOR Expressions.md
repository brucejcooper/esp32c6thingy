A CBOR expression is an expression that allows us to take a CBOR object (a source) and turn it into another CBOR object, potentially transforming data or making changes to it.


A CBOR object is a normal CBOR object, and a CBOR expression literal is just a CBOR object.  The only difference comes when we see a value with a special semantic tag (#6.).  When encountered, the expression executor will substitute the result of evaluating that expression with the value in the CBOR object.

The encapsulated data in the tag will always be an Array with at least one element.  The first element is a number that indicates the function to apply.  The array may have additional elements which are considered arguments to the function.  Arithmetic are considered the same way that other functions are.



## Function IDS
0. Self - The root object/parameter for the expression.
1. Index - Return a value of an array or map - Takes two arguments (source object and index, both expressions themselves)
2. Conditional execution ? operator - Takes three arguments - Source, truthy response, otherwise response
3. Filter - Run an expression on each element of an array. Create a new array with all the elements that return truthy.  tAkes two parameters (source and expression)
4. Map - Run an expression on each element and create a new array with those values. (source and expression)
5. Sequence: run a series of expressions in a row. Is this needed? Only if we have side effects or variables, which we shouldn't
6. Arithmetic +: Expects two parameters (left and right) - Parameters may be 
    1. Two numbers (add them together)
    1. A string and another thing (concatenate)
    1. Two arrays (concatenate)
7. Arithmetic -: Only works on numbers.
8. Arithmetic *: Only works on numbers
9. Arithmetic /: Only works on numbers
10. Arithmetic !: Negation
11. Arithmetic |: Logical Or
12. Arithmetic &: Logical &
13. Arithmetic ==: Comparison
13. Arithmetic %: Modulo division
14. Return value.


As an example, lets look at what our actions will be for a dimmable light. 


typedef 

```js

click action = evt.clickCount == 1 ? [target, OnOff, Toggle] : evt.clickCount == 2 ? [target, Brightness, RECALL_MAX] : nil;

longpress action = [target, Brightness, Delta, evt.clickCount % 2 ? 1 : -1]

```

Need a stack on which to store values.
Walk through thingy.  
representation of 


on('click', evt => {
    switch (evt.clickCount == 1) {
        case 1:
            callService(target, ONOFF, TOGGLE);
        case 2: 
            callService(target, BRIGHTNESS, RECALL_MAX);
    }
});

on('long_press', evt => callService(target, BRIGHTNESS, DELTA, evt.clickCount % 2 ? -1 : 1))
```

these would translate to the following ASTs

```
expr(sequence, 
    expr(switch,
        expr(==, expr(index, expr(param0), clickCount), 1),
        1,
        expr(callFunction, "callService", [GTIN, ``, SN, ``], ONOFF, TOGGLE),
        2,
        expr(callFunction, "callService", [GTIN, ``, SN, ``], BRIGHTNESS, RECALL_MAX),
    )
    // Implicit return nil.
)
```

and

```
[       
    [GTIN, '', SN, ''],
    BRIGHTNESS,
    DELTA,
    expr(?,
        expr(%,
            expr(index, expr(self), clickCount),
            2
        ),
        -1,
        1
    )
]
```

This is _very_ expressive, but may be difficult to turn into a User interface. The user should be able to pick a target based on a drop down.  There's also the translation from textual IDs into numbers, which are context specific. 

A UI could have a "Condition", "Action", "parameter" section.


+--------------------------+
| Condition: ___________   |
| Action:    __________v   |
| Paramters: ===========   |
+--------------------------+

expression language should understand 

deviceID('gtin:abc23427d6d63/sn:ad5556656') -> []


Call Service turns arguments into array[]
setAttributes turns arguments into a map.
final outcome determines what happens
- Array means call service
- Map means set attributes
- nil or undefined means do nothing.