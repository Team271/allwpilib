// Copyright (c) FIRST and other WPILib contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

package edu.wpi.first.units;

public class Time extends Unit<Time> {
  /** Creates a new unit with the given name and multiplier to the base unit. */
  Time(double baseUnitEquivalent, String name, String symbol) {
    super(Time.class, baseUnitEquivalent, name, symbol);
  }

  Time(UnaryFunction toBaseConverter, UnaryFunction fromBaseConverter, String name, String symbol) {
    super(Time.class, toBaseConverter, fromBaseConverter, name, symbol);
  }
}
