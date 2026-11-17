from nam_pipeline.parameters import ParameterParseError, build_matrix, parse_tokens


def test_adjacent_tokens():
    assert parse_tokens("Amp-R0P10_Out.wav") == {"R": 0.0, "P": 10.0}


def test_separated_tokens():
    assert parse_tokens("gain_2_tone_8.wav") == {"gain": 2.0, "tone": 8.0}


def test_decimal_tokens():
    assert parse_tokens("R2.5P10_Out.wav") == {"R": 2.5, "P": 10.0}


def test_matrix_ignores_constant_device_token():
    files = [
        "Peavey6505-R0P0_Out.wav",
        "Peavey6505-R0P10_Out.wav",
        "Peavey6505-R10P0_Out.wav",
        "Peavey6505-R10P10_Out.wav",
    ]
    specs, captures = build_matrix(files)
    assert [s.name for s in specs] == ["R", "P"]
    assert captures[1].values == {"R": 0.0, "P": 10.0}


def test_missing_axis_rejected():
    files = ["A-R0P0_Out.wav", "A-R2_Out.wav"]
    try:
        build_matrix(files, axes=["R", "P"])
    except ParameterParseError:
        return
    raise AssertionError("expected ParameterParseError")


def test_duplicate_vector_rejected():
    files = ["A-R0P0_take1.wav", "A-R0P0_take2.wav"]
    try:
        build_matrix(files, axes=["R", "P"])
    except ParameterParseError:
        return
    raise AssertionError("expected ParameterParseError")
