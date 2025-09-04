interface TemperatureProps {
  temp: any;
}

/**
 * Numeric component that displays the temperature value.
 * 
 * @param {number} props.temp - The temperature value to be displayed.
 * @returns {JSX.Element} The rendered Numeric component.
 */
function Numeric({ temp }: TemperatureProps) {
  // TODO: Change the color of the text based on the temperature
  // HINT:
  //  - Consider using cn() from the utils folder for conditional tailwind styling
  //  - (or) Use the div's style prop to change the colour
  //  - (or) other solution

  // Justify your choice of implementation in brainstorming.md

  return (
  <div className={`text-6xl font-semibold ${
    temp < 20 || temp > 80
      ? "text-temp-unsafe"
      : (temp >= 20 && temp < 25) || (temp > 75 && temp <= 80)
      ? "text-temp-warn"
      : "text-temp-safe"
  }`}>
    {Number.isFinite(temp) ? temp.toFixed(3) : "—"}°C
  </div>
);

}

export default Numeric;
