# JUCE Prototyper

Instruo's JUCE-based DSP prototyping framework.

## Getting Started

Download this repo using GitHub's 'Download ZIP' option to your computer.
Extract the ZIP file and rename the extracted folder to something suitable, e.g.
`MyProject_prototype`.

Create an empty repository on GitHub, making sure to **not** add a README,
.gitignore, or license file.

In a terminal, navigate to the project folder and run the `setup.sh` script,
passing in suitable arguments.
Run the script with no arguments to see more detailed documentation on these; an
example would be:
```bash
bash setup.sh 'MyProject' https://github.com/InstruoModular/MyProject_prototype.git
```

You should then be able to open the folder as a workspace in VS Code, press F5,
and the project should build and run.


## Using the Prototyper

The project is set up to be as close to the standard Instruo project structure
as possible, with the exception of the `hardware` folder, which here is named
`gui`.

### Control Layout

In `gui/src/controls.cpp` there is a function called `build_gui_control_scheme`
which you'll use to build the control layout for your prototype.
This function is given a `GuiControlBuilder` object which has a number of
methods to add controls of various types.
These methods vary in what arguments they take, but all of them take an
`identifier` string, and a `label` string.

The `identifier` string must be unique, and valid as a JUCE-style identifier.
If running under a debugger, there are assertions to check for both of these.
You'll use this string later to access each parameter's value.

The `label` string is what will be displayed in the GUI to identify each
control.
This can read whatever you like, however it's probably sensible to keep it short
and easily identifiable.

The controls will appear on a grid in the GUI in the order they're added in the
code, left to right, top to bottom.
See the documention/code comments for examples and more details on how to use
this.


### Control Access

In `ParameterInterface::process` the input data struct contains a
`GuiControlData` object which you'll use to access the controls added to the
GUI.
This is split into separate containers for each control category to avoid type
confusion and unneccesary casting.
Each parameter's value is accessed by indexing the relevant container with the
`identifier` string given to the `GuiControlBuilder`, for example:
```c++
const float level = input.controls.sliders.at("output_level");
```

#### Slider Value Range

For each slider added to the `GuiControlBuilder` a set of text fields will be
added to the Settings pane in the GUI along with the slider's `label`.
The values given to these fields will be used to set the range for that slider's
control value as given to `ParameterInterface::process`.
The scaled value for each slider will be displayed within the slider.

**Note:** values entered into these text fields will only be applied when you
press Enter on your keyboard while the text box has keyboard focus.
Clicking away or pressing Escape will cause the text field to lose keyboard
focus, with the new value showing, but without having applied the change.
